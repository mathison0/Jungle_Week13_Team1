#pragma once

#include "Core/Types/CoreTypes.h"

struct ID3D11Device;

namespace nv
{
namespace cloth
{
	class Factory;
}
}

enum class EClothBackendPreference
{
	Auto,
	CUDA,
	DX11,
	CPU
};

enum class EClothBackend
{
	None,
	CUDA,
	DX11,
	CPU
};

struct FNvClothInitializeDesc
{
	EClothBackendPreference BackendPreference = EClothBackendPreference::Auto;
	ID3D11Device* D3DDevice = nullptr;
	bool bSynchronizeDxResources = false;
};

class FNvClothContext
{
public:
	FNvClothContext() = default;
	~FNvClothContext();

	bool Initialize(const FNvClothInitializeDesc& Desc);
	void Shutdown();

	bool IsInitialized() const { return Factory != nullptr; }
	EClothBackend GetActiveBackend() const { return ActiveBackend; }
	const FString& GetFallbackStatus() const { return FallbackStatus; }

	const char* GetActiveBackendName() const;
	nv::cloth::Factory* GetFactory() const { return Factory; }

private:
	struct FCudaDriverApi;
	class FDxContextManager;

	bool EnsureCallbacksInitialized();
	bool TryCreateCudaFactory(FString& InOutFallbackStatus);
	bool TryCreateDx11Factory(ID3D11Device* Device, bool bSynchronizeResources, FString& InOutFallbackStatus);
	bool TryCreateCpuFactory(FString& InOutFallbackStatus);

	void DestroyFactory();
	void ReleaseCuda();
	void ReleaseDx11();

	nv::cloth::Factory* Factory = nullptr;
	EClothBackend ActiveBackend = EClothBackend::None;
	FString FallbackStatus = "Not initialized";

	FCudaDriverApi* CudaApi = nullptr;
	void* CudaContext = nullptr;
	FDxContextManager* DxContextManager = nullptr;
};
