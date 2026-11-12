#include "mod/render/dx12_hook.h"

#include <windows.h>
#include <psapi.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>
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
// The plain DXGI class as a fallback (2026-11-12, see native_fallback_check).
std::atomic<uint64_t> g_installed_ms{0};          // when install() finished
std::atomic<uint64_t> g_last_upper_present_ms{0};  // the last Present of a class above DXGI's (Streamline, frame gen)
std::atomic<uint64_t> g_last_any_present_ms{0};    // the last Present through any hooked class
std::atomic<bool> g_native_tried{false};
std::atomic<bool> g_streamline_class{false};       // install() hooked Streamline's class (not DXGI's own)
std::atomic<int> g_dev_no_streamline{0};  // [dev] overlay_no_streamline: 1 Streamline's class is left unhooked, 2 and no fallback

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
//
// Every function this file has hooked (rt3b / pp8-loot, 2026-11-12: the fallback below hooked Streamline's Present a
// SECOND time on this PC, because the class a new swap chain gets here is the one already hooked).
std::atomic<void*> g_hooked[32]{};
std::atomic<int> g_hooked_n{0};
void note_hooked(void* target) {
    const int i = g_hooked_n.fetch_add(1);
    if (i < 32) g_hooked[i].store(target);
}
bool already_hooked(void* target) {
    const int n = (std::min)(g_hooked_n.load(), 32);
    for (int i = 0; i < n; ++i)
        if (g_hooked[i].load() == target) return true;
    return false;
}

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
    note_hooked(target);
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

std::atomic<uint64_t> g_execs{0};  // batches of drawing commands sent on a direct queue (the game is rendering)
void STDMETHODCALLTYPE hk_execute(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        t_last_direct_queue = q;
        g_execs.fetch_add(1, std::memory_order_relaxed);
    }
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
bool present_common(IDXGISwapChain* sc, UINT flags, bool native = false) {
    g_presents.fetch_add(1, std::memory_order_relaxed);
    g_last_any_present_ms.store(GetTickCount64(), std::memory_order_relaxed);
    // DXGI's own class under a class that presents too (frame generation presents the real swap chain from its own
    // thread): the class above draws, this one stays out of it.
    if (native) {
        if (GetTickCount64() - g_last_upper_present_ms.load(std::memory_order_relaxed) < 2000) return false;
    } else {
        g_last_upper_present_ms.store(GetTickCount64(), std::memory_order_relaxed);
    }
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
constexpr int kNativeHooks = 3;
template <int N>
struct ProxyHooks {
    static inline SafetyHookInline present, present1, resize, resize1;
    static inline PresentFn o_present = nullptr;
    static inline Present1Fn o_present1 = nullptr;
    static inline ResizeBuffersFn o_resize = nullptr;
    static inline ResizeBuffers1Fn o_resize1 = nullptr;
    static constexpr bool kNative = N == kNativeHooks;  // DXGI's own class (native_fallback_check)
    static HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
        const bool mine = present_common(sc, flags, kNative);
        HRESULT hr = o_present(sc, sync, flags);
        if (mine) t_in_present = false;
        return hr;
    }
    static HRESULT STDMETHODCALLTYPE hk_present1(IDXGISwapChain1* sc, UINT sync, UINT flags,
                                                 const DXGI_PRESENT_PARAMETERS* params) {
        const bool mine = present_common(sc, flags, kNative);
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
void** throwaway_swap_chain_vtable(IDXGIFactory4* factory, ID3D12CommandQueue* queue);
bool find_vtables(void**& swapchain_vtable, void**& queue_vtable) {
    if (HMODULE sl = GetModuleHandleW(L"sl.interposer.dll")) {
        swapchain_vtable = find_rtti_vtable(sl, ".?AUDXGISwapChain@interposer@sl@@");
        if (!swapchain_vtable) {
            log::error("Overlay: Streamline is loaded but its swap chain class was not found");
            return false;
        }
        log::info("Overlay: using Streamline's swap chain class (no throwaway swap chain)");
        g_streamline_class = true;
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
    swapchain_vtable = throwaway_swap_chain_vtable(factory.Get(), queue.Get());
    return swapchain_vtable != nullptr;
}

// The vtable of the class DXGI itself gives for a swap chain (a 64x64 one on a hidden window, released at once).
void** throwaway_swap_chain_vtable(IDXGIFactory4* factory, ID3D12CommandQueue* queue) {
    void** swapchain_vtable = nullptr;
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
        if (SUCCEEDED(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &swapchain))) {
            swapchain_vtable = *reinterpret_cast<void***>(swapchain.Get());
        }
        DestroyWindow(hwnd);
    }
    UnregisterClassW(wc.lpszClassName, inst);
    return swapchain_vtable;
}

// This process's own window in front, shown and of a game's size: a player is looking at the game.
bool game_window_in_front() {
    const HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (!w || !GetWindowThreadProcessId(w, &pid) || pid != GetCurrentProcessId()) return false;
    RECT rc{};
    return IsWindowVisible(w) && !IsIconic(w) && GetClientRect(w, &rc) && rc.right >= 640 && rc.bottom >= 360;
}

// The game's own window is what is on screen at its centre (not minimised, not covered there by another window).
bool game_window_uncovered() {
    struct Find {
        HWND best = nullptr;
        LONG area = 0;
    } f;
    EnumWindows(
        [](HWND w, LPARAM lp) -> BOOL {
            DWORD pid = 0;
            RECT rc{};
            if (!GetWindowThreadProcessId(w, &pid) || pid != GetCurrentProcessId() || !IsWindowVisible(w) || IsIconic(w) ||
                !GetClientRect(w, &rc) || rc.right < 640 || rc.bottom < 360)
                return TRUE;
            auto& f = *reinterpret_cast<Find*>(lp);
            if (rc.right * rc.bottom > f.area) {
                f.area = rc.right * rc.bottom;
                f.best = w;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&f));
    if (!f.best) return false;
    RECT rc{};
    GetClientRect(f.best, &rc);
    POINT c{rc.right / 2, rc.bottom / 2};
    if (!ClientToScreen(f.best, &c)) return false;
    const HWND at = WindowFromPoint(c);
    return at && GetAncestor(at, GA_ROOT) == f.best;
}

std::string module_of(void* f) {
    HMODULE m = nullptr;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(f), &m) ||
        !GetModuleFileNameW(m, path, MAX_PATH))
        return "?";
    std::wstring p = path;
    const size_t slash = p.find_last_of(L"/\\");
    if (slash != std::wstring::npos) p = p.substr(slash + 1);
    std::string out;
    for (const wchar_t c : p) out += c < 128 ? static_cast<char>(c) : '?';
    return std::format("{}+0x{:X}", out, reinterpret_cast<uintptr_t>(f) - reinterpret_cast<uintptr_t>(m));
}

// 2026-11-12, a tester with an AMD Radeon RX 7900 XTX (frame generation off, builds up to 3c46bec5): "Overlay:
// installed true | presents 0 drawn 0" for a whole session: no Co-op menu, no waiting screen. Streamline is loaded
// on every PC (the game ships it) and its swap chain class was hooked, but on that PC the game does not present
// through it (Streamline wraps the swap chain only when one of its plugins needs to, and DLSS frame generation is
// not available there), nor through a frame generation class: it presents through DXGI's own class, which only the
// "no Streamline" path hooked.
// So: when Streamline's class was hooked and NO Present at all has come through any hooked class some seconds
// after the hooks went in, with the game's window in front, DXGI's own class gets the hooks too. Its vtable comes
// from a throwaway swap chain; that crashed about one launch in five when DLSS frame generation picked it up
// (find_vtables), which is the case where presents come through Streamline's class and this never runs. Should a
// class above start presenting later (frame generation switched on), the DXGI hook stays out of its way
// (present_common).
// am2 (2026-11-12, this PC, Streamline's class left unhooked by the dev switch): the throwaway swap chain's class
// IS Streamline's here (it wraps what our own CreateSwapChainForHwnd makes too), so the throwaway gives the class a
// swap chain gets on this PC, which is what the game got. The trigger "8 s / 20 s without a frame, the game's window
// in front, asked every 5 s" took 76 s and 830 s with two copies; the watcher below does not need the window.
void hook_dxgi_class(const std::string& why) {
    log::warn("Overlay: {}: the game presents through a swap chain class that is not hooked (DXGI's own, on a PC "
              "where Streamline does not wrap it); hooking the class a new swap chain gets here",
              why);
    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
        log::error("Overlay: no D3D12 device / queue for the throwaway swap chain");
        return;
    }
    void** vt = throwaway_swap_chain_vtable(factory.Get(), queue.Get());
    if (!vt) {
        log::error("Overlay: the throwaway swap chain could not be created");
        return;
    }
    log::info("Overlay: a new swap chain's class here: Present at {}, Present1 at {}", module_of(vt[kSlotPresent]),
              module_of(vt[kSlotPresent1]));
    if (already_hooked(vt[kSlotPresent])) {
        // The class a swap chain gets here IS the one hooked at install (Streamline wraps it on this PC): there is
        // nothing else to hook, the frames have simply not come (a window in the background presents nothing).
        // Hooking it again made every frame pass our Present twice (presents counted double) in two runs of the
        // posted 47135b83.
        log::info("Overlay: that class is the one already hooked here: nothing to hook, frames have just not come");
        return;
    }
    const bool ok = ProxyHooks<kNativeHooks>::install(vt, "DXGI");
    log::info("Overlay: that swap chain class {}", ok ? "is hooked: the overlay draws through it"
                                                       : "could NOT be hooked");
}

// Trigger 1 (the watcher thread, from install() on): the game sends drawing commands (ExecuteCommandLists on a direct
// queue, hooked at install for this) and for 4 s not one Present comes through a hooked class.
void native_fallback_watch() {
    log::info("Overlay: watching for frames (DLSS frame generation's plugin sl.dlss_g.dll {} loaded)",
              GetModuleHandleW(L"sl.dlss_g.dll") ? "is" : "is not");
    uint64_t p0 = g_presents.load(), e0 = g_execs.load(), t0 = GetTickCount64();
    for (;;) {
        Sleep(500);
        if (g_native_tried.load() || g_dev_no_streamline.load() == 2) return;
        const uint64_t p = g_presents.load(), e = g_execs.load(), now = GetTickCount64();
        if (p != p0) {  // frames come through a hooked class
            p0 = p;
            e0 = e;
            t0 = now;
            continue;
        }
        // A covered window draws (201 batches in 4 s) and presents nothing, which looks like the tester's PC and
        // set this off on an Nvidia card in rt3b and pp8-loot (the copy whose window went behind the other at that
        // moment). For a player that is alt-tab during start-up. What it leads to is a throwaway swap chain, and
        // that is only dangerous where DLSS frame generation's plugin is loaded (it crashed one launch in five when
        // the plugin picked the throwaway up). So: where that plugin is loaded, only while the game's window can
        // be seen; everywhere else (the tester's AMD card; the test's simulation of it) at once, window or not.
        // "In front" or "can be seen" for everyone took 75 s and 830 s in the two-copy test (nb1, nb2, am2: a game
        // started by a script never gets the foreground, and on this desktop a system dialog covers the centre).
        const bool dlss_g = GetModuleHandleW(L"sl.dlss_g.dll") != nullptr && g_dev_no_streamline.load() == 0;
        if (dlss_g && !game_window_uncovered()) {
            e0 = e;
            t0 = now;
            continue;
        }
        if (now - t0 < 4000) continue;
        if (e - e0 >= 80) {
            if (!g_native_tried.exchange(true))
                hook_dxgi_class(std::format("{} batches of drawing commands in {:.1f} s and no frame through a hooked "
                                            "swap chain class ({} frames before that)",
                                            e - e0, (now - t0) / 1000.0, p));
            return;
        }
        e0 = e;  // not rendering either: look again
        t0 = now;
    }
}

// Trigger 2 (asked with the overlay's status, every 5 s; for a PC where the command queue hook sees nothing): no
// frame for 8 s (20 s where DLSS frame generation's plugin is loaded: a throwaway swap chain it picks up crashed
// one launch in five) with the game's window in front.
void native_fallback_check() {
    if (!g_proxy_ready.load() || !g_streamline_class.load() || g_native_tried.load()) return;
    if (g_dev_no_streamline.load() == 2) return;  // dev: the tester's state, kept (no frame through any hook)
    const uint64_t since = (std::max)(g_installed_ms.load(), g_last_any_present_ms.load(std::memory_order_relaxed));
    const uint64_t waited = GetTickCount64() - since;
    const bool dlss_g = GetModuleHandleW(L"sl.dlss_g.dll") != nullptr;
    if (waited < (dlss_g ? 20000u : 8000u) || !game_window_in_front()) return;
    if (g_native_tried.exchange(true)) return;
    std::thread([waited] {
        hook_dxgi_class(std::format("no frame through a hooked swap chain class for {} s with the game in front", waited / 1000));
    }).detach();
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
    const bool skip = g_dev_no_streamline.load() != 0 && g_streamline_class.load();
    if (skip) log::warn("Overlay: [dev] overlay_no_streamline={}: Streamline's swap chain class is NOT hooked (1: a test of the DXGI fallback; 2: no fallback either)", g_dev_no_streamline.load());
    const bool ok = skip ||
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
    g_installed_ms = GetTickCount64();
    g_proxy_ready = true;
    hook_frame_generation_proxies();
    if (g_streamline_class.load()) {
        // The watcher's evidence that the game renders (on this PC the hook went in at the first frame anyway: the
        // queue is always learnt this way with Streamline).
        if (!g_execute_hooked.exchange(true)) {
            hook_function(g_queue_vtable[kSlotExecuteCommandLists], reinterpret_cast<void*>(&hk_execute),
                          g_execute_hook, o_execute, "ExecuteCommandLists");
        }
        std::thread(native_fallback_watch).detach();
    }
    return true;
}

void dev_no_streamline(int mode) { g_dev_no_streamline = mode; }

Status status() {
    hook_frame_generation_proxies();  // a library loaded since (frame generation switched on in the settings)
    native_fallback_check();          // no frame through any hooked class: DXGI's own class (AMD cards)
    std::lock_guard lock(g_status_mutex);
    Status s = g_status;
    s.disabled = g_disabled;
    s.presents = g_presents.load(std::memory_order_relaxed);
    s.frames_drawn = g_frames.load(std::memory_order_relaxed);
    return s;
}

}  // namespace cdmp::render
