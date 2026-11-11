#include "mod/render/dx12_hook.h"

#include <windows.h>
#include <psapi.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "mod/log.h"
#include "mod/render/game_ui.h"

using Microsoft::WRL::ComPtr;

namespace cdmp::render {
namespace {

// vtable slots (IUnknown 0-2, IDXGIObject 3-6, IDXGIDeviceSubObject 7, ...).
constexpr int kSlotPresent = 8;
constexpr int kSlotResizeBuffers = 13;
constexpr int kSlotPresent1 = 22;
constexpr int kSlotResizeBuffers1 = 39;
constexpr int kSlotExecuteCommandLists = 10;  // ID3D12CommandQueue

constexpr UINT kSrvDescriptors = 256;
// If the swap chain we draw on stops presenting while another one keeps going
// (the game recreated it), switch after this many foreign presents.
constexpr int kSwitchAfterForeignPresents = 120;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT,
                                               const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT,
                                                    DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT,
                                                     DXGI_FORMAT, UINT, const UINT*,
                                                     IUnknown* const*);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT,
                                                       ID3D12CommandList* const*);

PresentFn o_present = nullptr;
Present1Fn o_present1 = nullptr;
ResizeBuffersFn o_resize = nullptr;
ResizeBuffers1Fn o_resize1 = nullptr;
ExecuteCommandListsFn o_execute = nullptr;
void** g_queue_vtable = nullptr;
std::atomic<bool> g_execute_hooked{false};
thread_local ID3D12CommandQueue* t_last_direct_queue = nullptr;
thread_local bool t_in_present = false;
volatile int g_stage = 0;  // where the render hook is (said when it faults)

DrawFn g_draw = nullptr;
CRITICAL_SECTION g_render_cs;
std::atomic<bool> g_disabled{false};
std::atomic<uint64_t> g_presents{0};
std::atomic<uint64_t> g_frames{0};

std::mutex g_status_mutex;
Status g_status;

void note(const std::string& queue_source, const std::string& error) {
    std::lock_guard lock(g_status_mutex);
    if (!queue_source.empty()) g_status.queue_source = queue_source;
    if (!error.empty()) g_status.last_error = error;
}

void disable(const std::string& why) {
    if (!g_disabled.exchange(true)) {
        log::error("Overlay disabled: {}", why);
        note({}, why);
    }
}

// Inline-hooks the function a vtable slot points to. Patching the vtable slot
// itself is not enough in this game: Streamline / DLSS frame generation call
// DXGI's Present directly, never through the vtable (verified in game: 0 calls
// through a patched slot). Hooking the code catches every caller.
template <class Fn>
bool hook_function(void* target, void* detour, SafetyHookInline& hook, Fn& original,
                   const char* name) {
    uint8_t head[5] = {};
    std::memcpy(head, target, sizeof head);
    hook = safetyhook::create_inline(target, detour, SafetyHookInline::StartDisabled);
    if (!hook) {
        log::error("Overlay: cannot hook {} at 0x{:X}", name, reinterpret_cast<uintptr_t>(target));
        return false;
    }
    original = hook.original<Fn>();  // publish the trampoline before enabling
    if (!hook.enable()) {
        log::error("Overlay: cannot enable hook on {}", name);
        return false;
    }
    HMODULE mod = nullptr;
    wchar_t path[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(target), &mod))
        GetModuleFileNameW(mod, path, MAX_PATH);
    const wchar_t* file = wcsrchr(path, L'\\');
    std::string module;  // module names are ASCII
    for (const wchar_t* c = file ? file + 1 : path; *c; ++c) module += static_cast<char>(*c);
    log::info("Overlay: hooked {} at 0x{:X} = {}+0x{:X} (first bytes {:02X} {:02X} {:02X} {:02X} {:02X})", name,
              reinterpret_cast<uintptr_t>(target), module,
              reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(mod), head[0], head[1], head[2],
              head[3], head[4]);
    return true;
}

SafetyHookInline g_present_hook;
SafetyHookInline g_present1_hook;
SafetyHookInline g_resize_hook;
SafetyHookInline g_resize1_hook;
SafetyHookInline g_execute_hook;

void STDMETHODCALLTYPE hk_execute(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) t_last_direct_queue = q;
    o_execute(q, n, lists);
}

// ------------------------------------------------------------------ renderer

struct Renderer {
    IDXGISwapChain* owner = nullptr;  // identity only; never AddRef'd
    int foreign_presents = 0;

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<ID3D12DescriptorHeap> srv_heap;
    UINT rtv_size = 0;
    UINT srv_size = 0;
    std::vector<bool> srv_used;
    std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
    std::vector<UINT64> allocator_fence;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    UINT64 fence_value = 0;

    UINT buffer_count = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool imgui_ready = false;
    LARGE_INTEGER last_frame{};
};

Renderer g_r;
ImGuiContext* g_imgui = nullptr;

void srv_alloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
               D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    for (UINT i = 0; i < kSrvDescriptors; ++i) {
        if (g_r.srv_used[i]) continue;
        g_r.srv_used[i] = true;
        cpu->ptr = g_r.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + i * g_r.srv_size;
        gpu->ptr = g_r.srv_heap->GetGPUDescriptorHandleForHeapStart().ptr + i * g_r.srv_size;
        return;
    }
    // Out of descriptors: hand out slot 0 rather than garbage (glyphs may glitch).
    *cpu = g_r.srv_heap->GetCPUDescriptorHandleForHeapStart();
    *gpu = g_r.srv_heap->GetGPUDescriptorHandleForHeapStart();
}

void srv_free(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
              D3D12_GPU_DESCRIPTOR_HANDLE) {
    const UINT i = static_cast<UINT>(
        (cpu.ptr - g_r.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr) / g_r.srv_size);
    if (i < kSrvDescriptors) g_r.srv_used[i] = false;
}

void wait_gpu_idle() {
    if (!g_r.fence || !g_r.queue) return;
    const UINT64 v = ++g_r.fence_value;
    if (FAILED(g_r.queue->Signal(g_r.fence.Get(), v))) return;
    if (g_r.fence->GetCompletedValue() < v) {
        g_r.fence->SetEventOnCompletion(v, g_r.fence_event);
        WaitForSingleObject(g_r.fence_event, 2000);
    }
}

void shutdown_renderer() {
    wait_gpu_idle();
    if (g_r.imgui_ready) ImGui_ImplDX12_Shutdown();
    if (g_r.fence_event) CloseHandle(g_r.fence_event);
    g_r = Renderer{};
}

// The swap chain's command queue through GetDevice, checked: a frame generation proxy (XeSS-FG, tcB408) answers the
// request for an ID3D12CommandQueue with S_OK and an object that is not one (the first call on it faulted). So the
// answer is taken as IUnknown and asked for the queue interface itself; anything odd = no queue (the fallback).
ID3D12CommandQueue* queue_of(IDXGISwapChain* sc) noexcept {
    ID3D12CommandQueue* q = nullptr;
    __try {
        IUnknown* u = nullptr;
        if (SUCCEEDED(sc->GetDevice(__uuidof(IUnknown), reinterpret_cast<void**>(&u))) && u) {
            if (FAILED(u->QueryInterface(__uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&q)))) q = nullptr;
            u->Release();
        }
        if (q) (void)q->GetDesc();  // must behave like a queue
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        q = nullptr;
    }
    return q;
}

ComPtr<ID3D12CommandQueue> find_queue(IDXGISwapChain* sc, std::string& source) {
    ComPtr<ID3D12CommandQueue> queue;
    g_stage = 10;
    // For D3D12 swap chains, GetDevice() hands back the queue the chain was
    // created with, which is exactly the queue we must submit on.
    if (ID3D12CommandQueue* q = queue_of(sc)) {
        queue.Attach(q);
        source = "swapchain->GetDevice";
        return queue;
    }
    // Fallback: the last direct queue that executed on this (present) thread.
    if (!g_execute_hooked.exchange(true)) {
        log::info("Overlay: GetDevice gave no queue; watching ExecuteCommandLists instead");
        hook_function(g_queue_vtable[kSlotExecuteCommandLists],
                      reinterpret_cast<void*>(&hk_execute), g_execute_hook, o_execute,
                      "ExecuteCommandLists");
    }
    if (t_last_direct_queue) {
        queue = t_last_direct_queue;
        source = "ExecuteCommandLists (present thread)";
    }
    return queue;
}

bool init_renderer(IDXGISwapChain* sc, const DXGI_SWAP_CHAIN_DESC& desc) {
    std::string source;
    ComPtr<ID3D12CommandQueue> queue = find_queue(sc, source);
    g_stage = 11;
    if (!queue) return false;  // try again next frame
    if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        disable("present queue is not a direct queue");
        return false;
    }

    g_stage = 12;
    Renderer r;
    r.owner = sc;
    r.queue = queue;
    if (FAILED(queue->GetDevice(IID_PPV_ARGS(&r.device)))) return disable("no device"), false;
    r.buffer_count = desc.BufferCount;
    r.format = desc.BufferDesc.Format;

    D3D12_DESCRIPTOR_HEAP_DESC rtv{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, r.buffer_count,
                                   D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC srv{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvDescriptors,
                                   D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(r.device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&r.rtv_heap))) ||
        FAILED(r.device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&r.srv_heap)))) {
        return disable("descriptor heap creation failed"), false;
    }
    r.rtv_size = r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    r.srv_size = r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    r.srv_used.assign(kSrvDescriptors, false);

    r.allocators.resize(r.buffer_count);
    r.allocator_fence.assign(r.buffer_count, 0);
    for (auto& a : r.allocators) {
        if (FAILED(r.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&a)))) {
            return disable("command allocator creation failed"), false;
        }
    }
    if (FAILED(r.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           r.allocators[0].Get(), nullptr,
                                           IID_PPV_ARGS(&r.list))) ||
        FAILED(r.list->Close()) ||
        FAILED(r.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r.fence)))) {
        return disable("command list / fence creation failed"), false;
    }
    g_stage = 13;
    r.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    QueryPerformanceCounter(&r.last_frame);
    g_r = std::move(r);

    if (!g_imgui) {
        IMGUI_CHECKVERSION();
        g_imgui = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;  // never write imgui.ini into the game folder
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();
        ui::load_fonts();  // Co-op+: the game's look (game_ui.h)
    }

    ImGui_ImplDX12_InitInfo info;
    info.Device = g_r.device.Get();
    info.CommandQueue = g_r.queue.Get();
    info.NumFramesInFlight = static_cast<int>(g_r.buffer_count);
    info.RTVFormat = g_r.format;
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.SrvDescriptorHeap = g_r.srv_heap.Get();
    info.SrvDescriptorAllocFn = srv_alloc;
    info.SrvDescriptorFreeFn = srv_free;
    if (!ImGui_ImplDX12_Init(&info)) return disable("ImGui DX12 init failed"), false;
    g_r.imgui_ready = true;

    note(source, {});
    log::info("Overlay ready: {} buffers, format {}, {}x{}, queue via {}", desc.BufferCount,
              static_cast<int>(desc.BufferDesc.Format), desc.BufferDesc.Width,
              desc.BufferDesc.Height, source);
    return true;
}

void draw_frame(IDXGISwapChain* sc, const DXGI_SWAP_CHAIN_DESC& desc) {
    g_stage = 20;
    ComPtr<IDXGISwapChain3> sc3;
    if (FAILED(sc->QueryInterface(IID_PPV_ARGS(&sc3)))) return disable("no IDXGISwapChain3");
    g_stage = 21;
    const UINT idx = sc3->GetCurrentBackBufferIndex();
    g_stage = 22;
    if (idx >= g_r.buffer_count) return;

    // Make sure the GPU is done with this buffer's allocator from last time.
    if (g_r.fence->GetCompletedValue() < g_r.allocator_fence[idx]) {
        g_r.fence->SetEventOnCompletion(g_r.allocator_fence[idx], g_r.fence_event);
        if (WaitForSingleObject(g_r.fence_event, 1000) != WAIT_OBJECT_0) return;  // skip frame
    }

    g_stage = 23;
    ComPtr<ID3D12Resource> back_buffer;
    if (FAILED(sc->GetBuffer(idx, IID_PPV_ARGS(&back_buffer)))) return;
    g_stage = 24;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_r.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(idx) * g_r.rtv_size;
    g_r.device->CreateRenderTargetView(back_buffer.Get(), nullptr, rtv);

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = static_cast<float>(now.QuadPart - g_r.last_frame.QuadPart) / freq.QuadPart;
    g_r.last_frame = now;

    const float w = static_cast<float>(desc.BufferDesc.Width);
    const float h = static_cast<float>(desc.BufferDesc.Height);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(w, h);
    io.DeltaTime = dt > 0.0f && dt < 1.0f ? dt : 1.0f / 60.0f;
    g_stage = 25;
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    if (g_draw) g_draw(w, h);
    ImGui::Render();

    g_stage = 26;
    auto& alloc = g_r.allocators[idx];
    if (FAILED(alloc->Reset()) || FAILED(g_r.list->Reset(alloc.Get(), nullptr))) return;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = back_buffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_r.list->ResourceBarrier(1, &barrier);
    g_r.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {g_r.srv_heap.Get()};
    g_r.list->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_r.list.Get());
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    g_r.list->ResourceBarrier(1, &barrier);
    if (FAILED(g_r.list->Close())) return disable("command list Close failed");

    g_stage = 27;
    ID3D12CommandList* lists[] = {g_r.list.Get()};
    g_r.queue->ExecuteCommandLists(1, lists);
    g_stage = 28;
    g_r.queue->Signal(g_r.fence.Get(), ++g_r.fence_value);
    g_r.allocator_fence[idx] = g_r.fence_value;
    g_frames.fetch_add(1, std::memory_order_relaxed);
}

void on_present(IDXGISwapChain* sc) {
    g_stage = 1;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc))) return;
    g_stage = 2;
    // Ignore tiny helper swap chains; the game's is at least a few hundred px.
    if (desc.BufferDesc.Width < 320 || desc.BufferDesc.Height < 240) return;

    if (g_r.owner && sc != g_r.owner) {
        if (++g_r.foreign_presents < kSwitchAfterForeignPresents) return;
        log::info("Overlay: switching to a new swap chain");
        shutdown_renderer();
    }
    if (g_r.owner == sc) g_r.foreign_presents = 0;

    // Rebuild if the chain changed shape (buffer count / format).
    if (g_r.owner == sc &&
        (desc.BufferCount != g_r.buffer_count || desc.BufferDesc.Format != g_r.format)) {
        log::info("Overlay: swap chain changed ({} buffers, format {}), rebuilding",
                  desc.BufferCount, static_cast<int>(desc.BufferDesc.Format));
        shutdown_renderer();
    }
    if (!g_r.owner && !init_renderer(sc, desc)) return;
    draw_frame(sc, desc);
}

// SEH wrapper: no C++ objects with destructors allowed in this function.
void guarded_present(IDXGISwapChain* sc) {
    EnterCriticalSection(&g_render_cs);
    DWORD code = 0;
    __try {
        on_present(sc);
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
        static char line[160];
        std::snprintf(line, sizeof line, "Overlay disabled: exception 0x%08lX inside the render hook (stage %d)", code,
                      static_cast<int>(g_stage));
        log::write(log::Level::Error, line);
    }
    LeaveCriticalSection(&g_render_cs);
}

void before_resize(IDXGISwapChain* sc) {
    EnterCriticalSection(&g_render_cs);
    __try {
        // We hold no back-buffer references, so only make sure our commands on
        // the old buffers have finished.
        if (sc == g_r.owner) wait_gpu_idle();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
    }
    LeaveCriticalSection(&g_render_cs);
}

// ------------------------------------------------------------------- hooks

// True = this call took the "in present" mark (and drew): the caller clears it after the original returns. A hook
// nested inside another one's Present (Present -> Present1, a frame generation proxy -> the class under it) draws
// nothing and leaves the outer mark alone.
bool present_common(IDXGISwapChain* sc, UINT flags) {
    g_presents.fetch_add(1, std::memory_order_relaxed);
    if (g_disabled || (flags & DXGI_PRESENT_TEST) || t_in_present) return false;
    t_in_present = true;  // Present may call Present1 internally; draw once
    guarded_present(sc);
    return true;
}

HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    const bool mine = present_common(sc, flags);
    HRESULT hr = o_present(sc, sync, flags);
    if (mine) t_in_present = false;
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_present1(IDXGISwapChain1* sc, UINT sync, UINT flags,
                                      const DXGI_PRESENT_PARAMETERS* params) {
    const bool mine = present_common(sc, flags);
    HRESULT hr = o_present1(sc, sync, flags, params);
    if (mine) t_in_present = false;
    return hr;
}

// ---- frame generation proxies (2026-11-11, testers: no Co-op menu with Intel XeSS Frame Generation on; reproduced
// in tcB406: "presents 0 drawn 0"). With XeSS-FG or FSR frame generation the game presents through that library's own
// swap chain class, not through Streamline's, so the hooks above never ran and nothing of ours was drawn. Each
// such class gets the same hooks (its own originals); the overlay is drawn into the game's frame before the
// library takes it. The libraries are loaded when the option is on, possibly after us: looked for again later.
template <int N>
struct ProxyHooks {
    static inline SafetyHookInline present, present1, resize, resize1;
    static inline PresentFn o_present = nullptr;
    static inline Present1Fn o_present1 = nullptr;
    static inline ResizeBuffersFn o_resize = nullptr;
    static inline ResizeBuffers1Fn o_resize1 = nullptr;
    static HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
        const bool mine = present_common(sc, flags);
        HRESULT hr = o_present(sc, sync, flags);
        if (mine) t_in_present = false;
        return hr;
    }
    static HRESULT STDMETHODCALLTYPE hk_present1(IDXGISwapChain1* sc, UINT sync, UINT flags,
                                                 const DXGI_PRESENT_PARAMETERS* params) {
        const bool mine = present_common(sc, flags);
        HRESULT hr = o_present1(sc, sync, flags, params);
        if (mine) t_in_present = false;
        return hr;
    }
    static HRESULT STDMETHODCALLTYPE hk_resize(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt,
                                               UINT flags) {
        before_resize(sc);
        return o_resize(sc, count, w, h, fmt, flags);
    }
    static HRESULT STDMETHODCALLTYPE hk_resize1(IDXGISwapChain3* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt,
                                                UINT flags, const UINT* masks, IUnknown* const* queues) {
        before_resize(sc);
        return o_resize1(sc, count, w, h, fmt, flags, masks, queues);
    }
    // A stub that returns within its first five bytes ("xor eax, eax; ret": FSR's ResizeBuffers1, tcB407) is left
    // alone: it is too short for a jump, and the linker may have folded other identical functions onto it.
    static bool stub(void* f) {
        const auto* b = static_cast<const uint8_t*>(f);
        for (int i = 0; i < 5; ++i) {
            if (b[i] == 0xC3) return true;
        }
        return false;
    }
    static bool install(void** vt, const char* what) {
        const std::string n = what;
        bool ok = true;
        if (!stub(vt[kSlotResizeBuffers]))
            ok = hook_function(vt[kSlotResizeBuffers], reinterpret_cast<void*>(&hk_resize), resize, o_resize,
                               (n + " ResizeBuffers").c_str()) && ok;
        if (!stub(vt[kSlotResizeBuffers1]))
            ok = hook_function(vt[kSlotResizeBuffers1], reinterpret_cast<void*>(&hk_resize1), resize1, o_resize1,
                               (n + " ResizeBuffers1").c_str()) && ok;
        if (stub(vt[kSlotPresent])) return false;  // nothing to draw on
        ok = hook_function(vt[kSlotPresent], reinterpret_cast<void*>(&hk_present), present, o_present,
                           (n + " Present").c_str()) && ok;
        if (!stub(vt[kSlotPresent1]))
            ok = hook_function(vt[kSlotPresent1], reinterpret_cast<void*>(&hk_present1), present1, o_present1,
                               (n + " Present1").c_str()) && ok;
        return ok;
    }
};

HRESULT STDMETHODCALLTYPE hk_resize(IDXGISwapChain* sc, UINT count, UINT w, UINT h,
                                    DXGI_FORMAT fmt, UINT flags) {
    before_resize(sc);
    return o_resize(sc, count, w, h, fmt, flags);
}

HRESULT STDMETHODCALLTYPE hk_resize1(IDXGISwapChain3* sc, UINT count, UINT w, UINT h,
                                     DXGI_FORMAT fmt, UINT flags, const UINT* masks,
                                     IUnknown* const* queues) {
    before_resize(sc);
    return o_resize1(sc, count, w, h, fmt, flags, masks, queues);
}

// The vtable of a class with MSVC RTTI, found in a loaded module's image by its type name (e.g.
// ".?AUDXGISwapChain@interposer@sl@@"): type descriptor -> complete object locator (signature 1,
// offset 0) -> the vtable that the locator precedes. nullptr if not found.
void** find_rtti_vtable(HMODULE module, const char* type_name) {
    const auto base = reinterpret_cast<const uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const size_t size = nt->OptionalHeader.SizeOfImage;
    const size_t len = std::strlen(type_name);
    auto find = [&](const void* pat, size_t n, size_t from) -> size_t {
        for (size_t i = from; i + n <= size; i += (n == len ? 1 : 4)) {
            if (std::memcmp(base + i, pat, n) == 0) return i;
        }
        return SIZE_MAX;
    };
    const size_t name_at = find(type_name, len, 0);
    if (name_at == SIZE_MAX || name_at < 0x10) return nullptr;
    const auto td = static_cast<uint32_t>(name_at - 0x10);
    for (size_t at = find(&td, 4, 0); at != SIZE_MAX; at = find(&td, 4, at + 4)) {
        if (at < 0x0C) continue;
        const auto* col = reinterpret_cast<const uint32_t*>(base + at - 0x0C);
        if (col[0] != 1 || col[1] != 0) continue;  // signature 1 (x64), primary base (offset 0)
        const uint64_t col_va = reinterpret_cast<uintptr_t>(col);
        for (size_t v = 0; v + 16 <= size; v += 8) {
            if (*reinterpret_cast<const uint64_t*>(base + v) == col_va) {
                return reinterpret_cast<void**>(const_cast<uint8_t*>(base + v + 8));
            }
        }
    }
    return nullptr;
}

struct ProxyClass {
    const wchar_t* module;
    const char* type_name;  // MSVC RTTI name of the library's swap chain class
    const char* what;
    bool (*install)(void**, const char*);
    bool done;
};
ProxyClass g_proxies[] = {
    {L"libxess_fg.dll", ".?AUXefgInterpolationSwapChain@@", "XeSS-FG", &ProxyHooks<0>::install, false},
    {L"amd_fidelityfx_framegeneration_dx12.dll", ".?AVFrameInterpolationSwapChainDX12Stable@@", "FSR-FG",
     &ProxyHooks<1>::install, false},
    {L"amd_fidelityfx_framegeneration_dx12.dll", ".?AVFrameInterpolationSwapChainDX12@@", "FSR-FG (2)",
     &ProxyHooks<2>::install, false},
};
std::mutex g_proxy_mutex;
std::atomic<bool> g_proxy_ready{false};  // the main hooks are in (install() finished)

// Hooks the frame generation libraries' swap chain classes that are loaded now and not hooked yet. Cheap when
// there is nothing to do (a module lookup per class).
void hook_frame_generation_proxies() {
    if (!g_proxy_ready.load()) return;
    std::lock_guard lock(g_proxy_mutex);
    for (ProxyClass& p : g_proxies) {
        if (p.done) continue;
        const HMODULE m = GetModuleHandleW(p.module);
        if (!m) continue;
        p.done = true;  // one try per class, whatever comes of it
        void** vt = find_rtti_vtable(m, p.type_name);
        if (!vt) continue;
        // The slots must point into that library (a wrong vtable would hook something else).
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof mi)) continue;
        const auto lo = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll), hi = lo + mi.SizeOfImage;
        bool inside = true;
        for (const int slot : {kSlotPresent, kSlotPresent1, kSlotResizeBuffers, kSlotResizeBuffers1}) {
            const auto f = reinterpret_cast<uintptr_t>(vt[slot]);
            inside = inside && f >= lo && f < hi;
        }
        if (!inside) {
            log::warn("Overlay: {}'s swap chain class found, but its vtable does not look right: not hooked", p.what);
            continue;
        }
        const bool ok = p.install(vt, p.what);
        log::info("Overlay: frame generation ({}): its swap chain class {}", p.what,
                  ok ? "is hooked too: the overlay draws with it on" : "could NOT be hooked");
    }
}

// Learns the vtable addresses of the swap chain class the game presents through and of D3D12's
// command queue class.
//
// Co-op+ (3460): the game loads NVIDIA Streamline, which wraps every DXGI swap chain in its own
// proxy (sl::interposer::DXGISwapChain; the overlay has always ended up hooking that class's
// Present). Creating a throwaway swap chain while the game runs, D3D12 or D3D11, is picked up by
// DLSS frame generation and crashed the game a moment later in about one launch in five
// (sl.dlss_g.dll on our thread, sl.interposer.dll / d3d11.dll on the game's). With Streamline
// loaded, the proxy's vtable is read from its RTTI instead and no swap chain is created. Without
// Streamline, the throwaway D3D12 swap chain as before. The command queue vtable still needs a
// D3D12 device and queue, but no swap chain.
bool find_vtables(void**& swapchain_vtable, void**& queue_vtable) {
    if (HMODULE sl = GetModuleHandleW(L"sl.interposer.dll")) {
        swapchain_vtable = find_rtti_vtable(sl, ".?AUDXGISwapChain@interposer@sl@@");
        if (!swapchain_vtable) {
            log::error("Overlay: Streamline is loaded but its swap chain class was not found");
            return false;
        }
        log::info("Overlay: using Streamline's swap chain class (no throwaway swap chain)");
    }

    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        ComPtr<IDXGIAdapter> warp;
        if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))
            D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!device || FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return false;
    queue_vtable = *reinterpret_cast<void***>(queue.Get());
    if (swapchain_vtable) return true;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = L"cdmp_dummy_window";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                nullptr, nullptr, inst, nullptr);
    if (hwnd) {
        ComPtr<IDXGISwapChain1> swapchain;
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = 64;
        sd.Height = 64;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &sd, nullptr, nullptr, &swapchain))) {
            swapchain_vtable = *reinterpret_cast<void***>(swapchain.Get());
        }
        DestroyWindow(hwnd);
    }
    UnregisterClassW(wc.lpszClassName, inst);
    return swapchain_vtable != nullptr;
}

}  // namespace

bool install(DrawFn draw) {
    InitializeCriticalSection(&g_render_cs);
    g_draw = draw;

    void** sc_vtable = nullptr;
    if (!find_vtables(sc_vtable, g_queue_vtable)) {
        log::error("Overlay: could not create the dummy swap chain / D3D12 queue");
        note({}, "dummy swap chain failed");
        return false;
    }
    // Resize hooks first: once Present is hooked we may start holding GPU
    // work that a resize has to wait for.
    const bool ok =
        hook_function(sc_vtable[kSlotResizeBuffers], reinterpret_cast<void*>(&hk_resize),
                      g_resize_hook, o_resize, "ResizeBuffers") &&
        hook_function(sc_vtable[kSlotResizeBuffers1], reinterpret_cast<void*>(&hk_resize1),
                      g_resize1_hook, o_resize1, "ResizeBuffers1") &&
        hook_function(sc_vtable[kSlotPresent], reinterpret_cast<void*>(&hk_present),
                      g_present_hook, o_present, "Present") &&
        hook_function(sc_vtable[kSlotPresent1], reinterpret_cast<void*>(&hk_present1),
                      g_present1_hook, o_present1, "Present1");
    if (!ok) {
        // Leave whatever did hook in place: each hook is complete on its own,
        // and an unhooked Present simply means nothing gets drawn.
        note({}, "inline hook failed");
        return false;
    }
    {
        std::lock_guard lock(g_status_mutex);
        g_status.installed = true;
    }
    log::info("Overlay hooks installed (swap chain vtable 0x{:X})",
              reinterpret_cast<uintptr_t>(sc_vtable));
    g_proxy_ready = true;
    hook_frame_generation_proxies();
    return true;
}

Status status() {
    hook_frame_generation_proxies();  // a library loaded since (frame generation switched on in the settings)
    std::lock_guard lock(g_status_mutex);
    Status s = g_status;
    s.disabled = g_disabled;
    s.presents = g_presents.load(std::memory_order_relaxed);
    s.frames_drawn = g_frames.load(std::memory_order_relaxed);
    return s;
}

}  // namespace cdmp::render
