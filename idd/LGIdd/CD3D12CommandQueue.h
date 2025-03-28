#pragma once

#include <Windows.h>
#include <wdf.h>
#include <wrl.h>
#include <d3d12.h>
#include <functional>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace Microsoft::WRL::Wrappers::HandleTraits;

class CD3D12CommandQueue
{
  private:
    const WCHAR * m_name = nullptr;

    ComPtr<ID3D12CommandQueue       > m_queue;
    ComPtr<ID3D12CommandAllocator   > m_allocator;
    ComPtr<ID3D12GraphicsCommandList> m_gfxList;
    ComPtr<ID3D12CommandList        > m_cmdList;
    ComPtr<ID3D12Fence              > m_fence;

    bool m_pending;
    HandleT<HANDLENullTraits> m_event;
    HANDLE m_waitHandle = INVALID_HANDLE_VALUE;
    UINT64 m_fenceValue = 0;

    typedef std::function<void(CD3D12CommandQueue * queue, void * param1, void * param2)> CompletionFunction;
    CompletionFunction m_completionCallback = nullptr;
    void * m_completionParams[2];

    bool Reset();
    void OnCompletion()
    {
      Reset();
      m_pending = false;
      if (m_completionCallback)
        m_completionCallback(
          this,
          m_completionParams[0],
          m_completionParams[1]);
    }

  public:
    ~CD3D12CommandQueue() { DeInit(); }

    bool Init(ID3D12Device3 * device, D3D12_COMMAND_LIST_TYPE type, const WCHAR * name);
    void DeInit();

    void SetCompletionCallback(CompletionFunction fn, void * param1, void * param2)
    {
      m_completionCallback  = fn;
      m_completionParams[0] = param1;
      m_completionParams[1] = param2;
    }

    bool Execute();

    //void Wait();
    bool IsReady() { return !m_pending; }
    HANDLE GetEvent() { return m_event.Get(); }

    ComPtr<ID3D12CommandQueue       > GetCmdQueue() { return m_queue;   }
    ComPtr<ID3D12GraphicsCommandList> GetGfxList()  { return m_gfxList; }
};