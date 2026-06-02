#include "Cloth/NvClothContext.h"

#include "Core/Logging/Log.h"
#include "Physics/PhysX/PhysXCore.h"

#if WITH_NVCLOTH
#include <NvCloth/Callbacks.h>
#include <NvCloth/DxContextManagerCallback.h>
#include <NvCloth/Factory.h>
#include <foundation/PxErrorCallback.h>

#include <Windows.h>
#include <d3d11.h>

#include <atomic>
#include <cstring>
#include <mutex>
#endif

#if WITH_NVCLOTH

static const char* GetNvClothLogFileName(const char* File)
{
	if (!File)
	{
		return "";
	}

	const char* LastBackslash = std::strrchr(File, '\\');
	const char* LastSlash = std::strrchr(File, '/');
	const char* LastSeparator = LastBackslash > LastSlash ? LastBackslash : LastSlash;
	return LastSeparator ? LastSeparator + 1 : File;
}

static std::atomic<bool> GNvClothBackendErrorReported = false;

static bool IsNvClothBackendBlockingError(physx::PxErrorCode::Enum Code)
{
	return Code == physx::PxErrorCode::eABORT ||
		Code == physx::PxErrorCode::eOUT_OF_MEMORY ||
		Code == physx::PxErrorCode::eINTERNAL_ERROR ||
		Code == physx::PxErrorCode::eINVALID_OPERATION ||
		Code == physx::PxErrorCode::eINVALID_PARAMETER;
}

static void ResetNvClothBackendError()
{
	GNvClothBackendErrorReported.store(false);
}

static bool DidNvClothBackendReportError()
{
	return GNvClothBackendErrorReported.load();
}

class FNvClothErrorCallback : public physx::PxErrorCallback
{
public:
	void reportError(physx::PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
	{
		if (IsNvClothBackendBlockingError(Code))
		{
			GNvClothBackendErrorReported.store(true);
		}

		const char* Severity = "Info";
		if (Code == physx::PxErrorCode::eABORT || Code == physx::PxErrorCode::eOUT_OF_MEMORY)
		{
			Severity = "Fatal";
		}
		else if (Code == physx::PxErrorCode::eINTERNAL_ERROR || Code == physx::PxErrorCode::eINVALID_OPERATION)
		{
			Severity = "Error";
		}
		else if (Code == physx::PxErrorCode::eINVALID_PARAMETER ||
			Code == physx::PxErrorCode::ePERF_WARNING ||
			Code == physx::PxErrorCode::eDEBUG_WARNING)
		{
			Severity = "Warning";
		}

		UE_LOG("[NvCloth %s] %s (%s:%d)", Severity, Message ? Message : "", GetNvClothLogFileName(File), Line);
	}
};

class FNvClothAssertHandler : public nv::cloth::PxAssertHandler
{
public:
	void operator()(const char* Exp, const char* File, int Line, bool& Ignore) override
	{
		UE_LOG("[NvCloth Assert] %s (%s:%d)", Exp ? Exp : "", GetNvClothLogFileName(File), Line);
		Ignore = false;
	}
};

static FNvClothErrorCallback GNvClothErrorCallback;
static FNvClothAssertHandler GNvClothAssertHandler;
static bool GNvClothCallbacksInitialized = false;

struct FNvClothContext::FCudaDriverApi
{
	using CUresult = int;
	using CUdevice = int;
	using CUcontext = ::CUcontext;

	using CuInitFn = CUresult(__stdcall*)(unsigned int);
	using CuDeviceGetCountFn = CUresult(__stdcall*)(int*);
	using CuDeviceGetFn = CUresult(__stdcall*)(CUdevice*, int);
	using CuCtxCreateFn = CUresult(__stdcall*)(CUcontext*, unsigned int, CUdevice);
	using CuCtxDestroyFn = CUresult(__stdcall*)(CUcontext);

	HMODULE Module = nullptr;
	CuInitFn CuInit = nullptr;
	CuDeviceGetCountFn CuDeviceGetCount = nullptr;
	CuDeviceGetFn CuDeviceGet = nullptr;
	CuCtxCreateFn CuCtxCreate = nullptr;
	CuCtxDestroyFn CuCtxDestroy = nullptr;

	bool Load()
	{
		Module = LoadLibraryA("nvcuda.dll");
		if (!Module) return false;

		CuInit = reinterpret_cast<CuInitFn>(GetProcAddress(Module, "cuInit"));
		CuDeviceGetCount = reinterpret_cast<CuDeviceGetCountFn>(GetProcAddress(Module, "cuDeviceGetCount"));
		CuDeviceGet = reinterpret_cast<CuDeviceGetFn>(GetProcAddress(Module, "cuDeviceGet"));
		CuCtxCreate = reinterpret_cast<CuCtxCreateFn>(GetProcAddress(Module, "cuCtxCreate_v2"));
		CuCtxDestroy = reinterpret_cast<CuCtxDestroyFn>(GetProcAddress(Module, "cuCtxDestroy_v2"));

		if (!CuInit || !CuDeviceGetCount || !CuDeviceGet || !CuCtxCreate || !CuCtxDestroy)
		{
			Unload();
			return false;
		}

		return true;
	}

	void Unload()
	{
		CuInit = nullptr;
		CuDeviceGetCount = nullptr;
		CuDeviceGet = nullptr;
		CuCtxCreate = nullptr;
		CuCtxDestroy = nullptr;

		if (Module)
		{
			FreeLibrary(Module);
			Module = nullptr;
		}
	}
};

class FNvClothContext::FDxContextManager : public nv::cloth::DxContextManagerCallback
{
public:
	FDxContextManager(ID3D11Device* InDevice, bool bInSynchronizeResources)
		: Device(InDevice)
		, bSynchronizeResources(bInSynchronizeResources)
	{
		if (Device)
		{
			Device->AddRef();
			Device->GetImmediateContext(&Context);
		}
	}

	~FDxContextManager() override
	{
		if (Context)
		{
			Context->Release();
			Context = nullptr;
		}

		if (Device)
		{
			Device->Release();
			Device = nullptr;
		}
	}

	void acquireContext() override
	{
		Mutex.lock();
	}

	void releaseContext() override
	{
		Mutex.unlock();
	}

	ID3D11Device* getDevice() const override
	{
		return Device;
	}

	ID3D11DeviceContext* getContext() const override
	{
		return Context;
	}

	bool synchronizeResources() const override
	{
		return bSynchronizeResources;
	}

	bool IsValid() const
	{
		return Device != nullptr && Context != nullptr;
	}

private:
	std::recursive_mutex Mutex;
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* Context = nullptr;
	bool bSynchronizeResources = false;
};

#endif

FNvClothContext::~FNvClothContext()
{
	Shutdown();
}

bool FNvClothContext::Initialize(const FNvClothInitializeDesc& Desc)
{
	Shutdown();

#if WITH_NVCLOTH
	if (!EnsureCallbacksInitialized())
	{
		FallbackStatus = "NvCloth callback initialization failed";
		UE_LOG("[NvCloth] %s", FallbackStatus.c_str());
		return false;
	}

	FString LocalFallbackStatus;
	const EClothBackendPreference Preference = Desc.BackendPreference;

	if (Preference == EClothBackendPreference::Auto || Preference == EClothBackendPreference::CUDA)
	{
		if (TryCreateCudaFactory(LocalFallbackStatus))
		{
			FallbackStatus = LocalFallbackStatus.empty() ? "None" : LocalFallbackStatus;
			UE_LOG("[NvCloth] Initialized with CUDA backend.");
			return true;
		}
	}

	if (Preference == EClothBackendPreference::Auto ||
		Preference == EClothBackendPreference::CUDA ||
		Preference == EClothBackendPreference::DX11)
	{
		if (TryCreateDx11Factory(Desc.D3DDevice, Desc.bSynchronizeDxResources, LocalFallbackStatus))
		{
			FallbackStatus = LocalFallbackStatus.empty() ? "None" : LocalFallbackStatus;
			UE_LOG("[NvCloth] Initialized with DX11 backend. Fallback=%s", FallbackStatus.c_str());
			return true;
		}
	}

	if (TryCreateCpuFactory(LocalFallbackStatus))
	{
		FallbackStatus = LocalFallbackStatus.empty() ? "None" : LocalFallbackStatus;
		UE_LOG("[NvCloth] Initialized with CPU backend. Fallback=%s", FallbackStatus.c_str());
		return true;
	}

	FallbackStatus = LocalFallbackStatus.empty() ? "All backends failed" : LocalFallbackStatus;
	UE_LOG("[NvCloth] Initialization failed. Fallback=%s", FallbackStatus.c_str());
	return false;
#else
	(void)Desc;
	ActiveBackend = EClothBackend::None;
	FallbackStatus = "WITH_NVCLOTH=0";
	UE_LOG("[NvCloth] Disabled at build time.");
	return false;
#endif
}

void FNvClothContext::Shutdown()
{
	DestroyFactory();
	ReleaseDx11();
	ReleaseCuda();

	ActiveBackend = EClothBackend::None;
	FallbackStatus = "Not initialized";
}

const char* FNvClothContext::GetActiveBackendName() const
{
	switch (ActiveBackend)
	{
	case EClothBackend::CUDA: return "CUDA";
	case EClothBackend::DX11: return "DX11";
	case EClothBackend::CPU: return "CPU";
	default: return "None";
	}
}

bool FNvClothContext::EnsureCallbacksInitialized()
{
#if WITH_NVCLOTH
	if (GNvClothCallbacksInitialized) return true;

	nv::cloth::InitializeNvCloth(
		&FPhysXCore::GetAllocatorCallback(),
		&GNvClothErrorCallback,
		&GNvClothAssertHandler,
		nullptr
	);

	GNvClothCallbacksInitialized = true;
	return true;
#else
	return false;
#endif
}

bool FNvClothContext::TryCreateCudaFactory(FString& InOutFallbackStatus)
{
#if WITH_NVCLOTH
	if (!NvClothCompiledWithCudaSupport())
	{
		InOutFallbackStatus += "CUDA not compiled -> ";
		return false;
	}

	CudaApi = new FCudaDriverApi();
	if (!CudaApi->Load())
	{
		ReleaseCuda();
		InOutFallbackStatus += "CUDA driver missing -> ";
		return false;
	}

	constexpr FCudaDriverApi::CUresult CudaSuccess = 0;
	if (CudaApi->CuInit(0) != CudaSuccess)
	{
		ReleaseCuda();
		InOutFallbackStatus += "CUDA init failed -> ";
		return false;
	}

	int DeviceCount = 0;
	if (CudaApi->CuDeviceGetCount(&DeviceCount) != CudaSuccess || DeviceCount <= 0)
	{
		ReleaseCuda();
		InOutFallbackStatus += "CUDA device missing -> ";
		return false;
	}

	FCudaDriverApi::CUdevice Device = 0;
	if (CudaApi->CuDeviceGet(&Device, 0) != CudaSuccess)
	{
		ReleaseCuda();
		InOutFallbackStatus += "CUDA device get failed -> ";
		return false;
	}

	FCudaDriverApi::CUcontext NewContext = nullptr;
	if (CudaApi->CuCtxCreate(&NewContext, 0, Device) != CudaSuccess || !NewContext)
	{
		ReleaseCuda();
		InOutFallbackStatus += "CUDA context failed -> ";
		return false;
	}

	CudaContext = NewContext;
	ResetNvClothBackendError();
	Factory = NvClothCreateFactoryCUDA(NewContext);
	const bool bBackendError = DidNvClothBackendReportError();
	if (!Factory || bBackendError)
	{
		DestroyFactory();
		ReleaseCuda();
		InOutFallbackStatus += bBackendError ? "CUDA factory reported error -> " : "CUDA factory failed -> ";
		return false;
	}

	ActiveBackend = EClothBackend::CUDA;
	return true;
#else
	(void)InOutFallbackStatus;
	return false;
#endif
}

bool FNvClothContext::TryCreateDx11Factory(ID3D11Device* Device, bool bSynchronizeResources, FString& InOutFallbackStatus)
{
#if WITH_NVCLOTH
	if (!NvClothCompiledWithDxSupport())
	{
		InOutFallbackStatus += "DX11 not compiled -> ";
		return false;
	}

	if (!Device)
	{
		InOutFallbackStatus += "DX11 device missing -> ";
		return false;
	}

	DxContextManager = new FDxContextManager(Device, bSynchronizeResources);
	if (!DxContextManager->IsValid())
	{
		ReleaseDx11();
		InOutFallbackStatus += "DX11 context failed -> ";
		return false;
	}

	ResetNvClothBackendError();
	Factory = NvClothCreateFactoryDX11(DxContextManager);
	const bool bBackendError = DidNvClothBackendReportError();
	if (!Factory || bBackendError)
	{
		DestroyFactory();
		ReleaseDx11();
		InOutFallbackStatus += bBackendError ? "DX11 factory reported error -> " : "DX11 factory failed -> ";
		return false;
	}

	ActiveBackend = EClothBackend::DX11;
	return true;
#else
	(void)Device;
	(void)bSynchronizeResources;
	(void)InOutFallbackStatus;
	return false;
#endif
}

bool FNvClothContext::TryCreateCpuFactory(FString& InOutFallbackStatus)
{
#if WITH_NVCLOTH
	ResetNvClothBackendError();
	Factory = NvClothCreateFactoryCPU();
	const bool bBackendError = DidNvClothBackendReportError();
	if (!Factory || bBackendError)
	{
		DestroyFactory();
		InOutFallbackStatus += bBackendError ? "CPU factory reported error" : "CPU factory failed";
		return false;
	}

	ActiveBackend = EClothBackend::CPU;
	return true;
#else
	(void)InOutFallbackStatus;
	return false;
#endif
}

void FNvClothContext::DestroyFactory()
{
#if WITH_NVCLOTH
	if (Factory)
	{
		NvClothDestroyFactory(Factory);
		Factory = nullptr;
	}
#endif
}

void FNvClothContext::ReleaseCuda()
{
#if WITH_NVCLOTH
	if (CudaApi)
	{
		if (CudaContext && CudaApi->CuCtxDestroy)
		{
			CudaApi->CuCtxDestroy(static_cast<FCudaDriverApi::CUcontext>(CudaContext));
			CudaContext = nullptr;
		}

		CudaApi->Unload();
		delete CudaApi;
		CudaApi = nullptr;
	}

	CudaContext = nullptr;
#endif
}

void FNvClothContext::ReleaseDx11()
{
#if WITH_NVCLOTH
	if (DxContextManager)
	{
		delete DxContextManager;
		DxContextManager = nullptr;
	}
#endif
}
