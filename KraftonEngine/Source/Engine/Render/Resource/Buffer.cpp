#include "Buffer.h"
#include "Engine/Profiling/Stats/MemoryStats.h"

void FMeshBuffer::Release()
{
	VertexBuffer.Release();
	IndexBuffer.Release();
}

FVertexBuffer::FVertexBuffer(FVertexBuffer&& Other) noexcept
	: Buffer(Other.Buffer)
	, VertexCount(Other.VertexCount)
	, Stride(Other.Stride)
{
	Other.Buffer = nullptr;
	Other.VertexCount = 0;
	Other.Stride = 0;
}

FVertexBuffer& FVertexBuffer::operator=(FVertexBuffer&& Other) noexcept
{
	if (this != &Other)
	{
		Release();
		Buffer = Other.Buffer;
		VertexCount = Other.VertexCount;
		Stride = Other.Stride;
		Other.Buffer = nullptr;
		Other.VertexCount = 0;
		Other.Stride = 0;
	}
	return *this;
}

void FVertexBuffer::Create(ID3D11Device* InDevice, const void* InData, uint32 InVertexCount, uint32 InByteWidth, uint32 InStride)
{
	Release();

	if (!InData || InByteWidth == 0)
	{
		VertexCount = 0;
		Stride = InStride;
		return;
	}

	D3D11_BUFFER_DESC vertexBufferDesc = {};
	vertexBufferDesc.ByteWidth = InByteWidth;
	vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA vertexBufferSRD = { InData };

	HRESULT hr = InDevice->CreateBuffer(&vertexBufferDesc, &vertexBufferSRD, &Buffer);
	if (FAILED(hr))
	{
		VertexCount = 0;
		Stride = InStride;
		return;
	}
	Buffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("VertexBuffer")), "VertexBuffer");

	VertexCount = InVertexCount;
	Stride = InStride;
	MemoryStats::AddVertexBufferMemory(InByteWidth);
}

void FVertexBuffer::Release()
{
	if (Buffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Buffer->GetDesc(&Desc);
		MemoryStats::SubVertexBufferMemory(Desc.ByteWidth);

		Buffer->Release();
		Buffer = nullptr;
	}
	VertexCount = 0;
}

ID3D11Buffer* FVertexBuffer::GetBuffer() const
{
	return Buffer;
}

FConstantBuffer::FConstantBuffer(FConstantBuffer&& Other) noexcept
	: Buffer(Other.Buffer)
{
	Other.Buffer = nullptr;
}

FConstantBuffer& FConstantBuffer::operator=(FConstantBuffer&& Other) noexcept
{
	if (this != &Other)
	{
		Release();
		Buffer = Other.Buffer;
		Other.Buffer = nullptr;
	}
	return *this;
}

void FConstantBuffer::Create(ID3D11Device* InDevice, uint32 InByteWidth)
{
	Create(InDevice, InByteWidth, "ConstantBuffer");
}

void FConstantBuffer::Create(ID3D11Device* InDevice, uint32 InByteWidth, const char* DebugName)
{
	Release();

	D3D11_BUFFER_DESC constantBufferDesc = {};

	constantBufferDesc.ByteWidth = (InByteWidth + 0xf) & 0xfffffff0;
	constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	InDevice->CreateBuffer(&constantBufferDesc, nullptr, &Buffer);
	Buffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen(DebugName ? DebugName : "ConstantBuffer")), DebugName ? DebugName : "ConstantBuffer");
}

void FConstantBuffer::Release()
{
	if (Buffer)
	{
		Buffer->Release();
		Buffer = nullptr;
	}
}

void FConstantBuffer::Update(ID3D11DeviceContext* InDeviceContext, const void* InData, uint32 InByteWidth)
{
	if (Buffer)
	{
		D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
		HRESULT hr = InDeviceContext->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR);
		if (FAILED(hr))
		{
			return;
		}

		std::memcpy(constantbufferMSR.pData, InData, InByteWidth);

		InDeviceContext->Unmap(Buffer, 0);
	}
}

ID3D11Buffer* FConstantBuffer::GetBuffer()
{
	return Buffer;
}


FIndexBuffer::FIndexBuffer(FIndexBuffer&& Other) noexcept
	: Buffer(Other.Buffer)
	, IndexCount(Other.IndexCount)
{
	Other.Buffer = nullptr;
	Other.IndexCount = 0;
}

FIndexBuffer& FIndexBuffer::operator=(FIndexBuffer&& Other) noexcept
{
	if (this != &Other)
	{
		Release();
		Buffer = Other.Buffer;
		IndexCount = Other.IndexCount;
		Other.Buffer = nullptr;
		Other.IndexCount = 0;
	}
	return *this;
}

void FIndexBuffer::Create(ID3D11Device* InDevice, const void* InData, uint32 InIndexCount, uint32 InByteWidth)
{
	Release();

	if (!InData || InByteWidth == 0 || InIndexCount == 0)
	{
		IndexCount = 0;
		return;
	}

	D3D11_BUFFER_DESC indexBufferDesc = {};

	indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	indexBufferDesc.ByteWidth = InByteWidth;	//	NOTE : Total byte width of the buffer, not the count of indices
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA indexBufferSRD = { InData };

	HRESULT hr = InDevice->CreateBuffer(&indexBufferDesc, &indexBufferSRD, &Buffer);
	if (FAILED(hr))
	{
		IndexCount = 0;
		return;
	}
	Buffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("IndexBuffer")), "IndexBuffer");
	IndexCount = InIndexCount;
	MemoryStats::AddIndexBufferMemory(InByteWidth);
}

void FIndexBuffer::Release()
{
	if (Buffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Buffer->GetDesc(&Desc);
		MemoryStats::SubIndexBufferMemory(Desc.ByteWidth);

		Buffer->Release();
		Buffer = nullptr;
	}
	IndexCount = 0;
}

ID3D11Buffer* FIndexBuffer::GetBuffer() const
{
	return Buffer;
}

// ============================================================
// FDynamicVertexBuffer
// ============================================================

bool FDynamicVertexBuffer::Create(ID3D11Device* InDevice, uint32 InMaxCount, uint32 InStride)
{
	if (InStride == 0)
	{
		return false;
	}
	if (!Buffer)
	{
		Stride = InStride;
	}
	if (!InDevice || InMaxCount == 0)
	{
		return false;
	}

	const uint64 ByteWidth = static_cast<uint64>(InStride) * static_cast<uint64>(InMaxCount);
	if (ByteWidth == 0 || ByteWidth > static_cast<uint64>(UINT32_MAX))
	{
		return false;
	}

	D3D11_BUFFER_DESC Desc = {};
	Desc.ByteWidth = static_cast<UINT>(ByteWidth);
	Desc.Usage = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	ID3D11Buffer* NewBuffer = nullptr;
	if (FAILED(InDevice->CreateBuffer(&Desc, nullptr, &NewBuffer)) || !NewBuffer)
	{
		return false;
	}

	NewBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("DynamicVertexBuffer")), "DynamicVertexBuffer");

	Release();
	Buffer = NewBuffer;
	Stride = InStride;
	MaxCount = InMaxCount;
	return true;
}

void FDynamicVertexBuffer::Release()
{
	if (Buffer) { Buffer->Release(); Buffer = nullptr; }
	MaxCount = 0;
}

bool FDynamicVertexBuffer::EnsureCapacity(ID3D11Device* InDevice, uint32 RequiredCount)
{
	if (RequiredCount == 0) return true;
	if (Buffer && RequiredCount <= MaxCount) return true;
	if (Stride == 0) return false;

	uint32 NewMax = MaxCount > 0 ? MaxCount : 256;
	while (NewMax < RequiredCount)
	{
		if (NewMax > UINT32_MAX / 2)
		{
			NewMax = RequiredCount;
			break;
		}
		NewMax *= 2;
	}
	return Create(InDevice, NewMax, Stride);
}

bool FDynamicVertexBuffer::Update(ID3D11DeviceContext* Context, const void* Data, uint32 Count)
{
	if (!Buffer || !Context || !Data || Count == 0) return false;
	if (Count > MaxCount) return false;
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(Context->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped))) return false;
	memcpy(Mapped.pData, Data, static_cast<size_t>(Stride) * Count);
	Context->Unmap(Buffer, 0);
	return true;
}

void FDynamicVertexBuffer::Bind(ID3D11DeviceContext* Context, uint32 Slot)
{
	UINT Offset = 0;
	Context->IASetVertexBuffers(Slot, 1, &Buffer, &Stride, &Offset);
}

// ============================================================
// FDynamicIndexBuffer
// ============================================================

bool FDynamicIndexBuffer::Create(ID3D11Device* InDevice, uint32 InMaxCount)
{
	if (!InDevice || InMaxCount == 0)
	{
		return false;
	}

	const uint64 ByteWidth = static_cast<uint64>(sizeof(uint32)) * static_cast<uint64>(InMaxCount);
	if (ByteWidth == 0 || ByteWidth > static_cast<uint64>(UINT32_MAX))
	{
		return false;
	}

	D3D11_BUFFER_DESC Desc = {};
	Desc.ByteWidth = static_cast<UINT>(ByteWidth);
	Desc.Usage = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	ID3D11Buffer* NewBuffer = nullptr;
	if (FAILED(InDevice->CreateBuffer(&Desc, nullptr, &NewBuffer)) || !NewBuffer)
	{
		return false;
	}

	NewBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("DynamicIndexBuffer")), "DynamicIndexBuffer");

	Release();
	Buffer = NewBuffer;
	MaxCount = InMaxCount;
	return true;
}

void FDynamicIndexBuffer::Release()
{
	if (Buffer) { Buffer->Release(); Buffer = nullptr; }
	MaxCount = 0;
}

bool FDynamicIndexBuffer::EnsureCapacity(ID3D11Device* InDevice, uint32 RequiredCount)
{
	if (RequiredCount == 0) return true;
	if (Buffer && RequiredCount <= MaxCount) return true;

	uint32 NewMax = MaxCount > 0 ? MaxCount : 256;
	while (NewMax < RequiredCount)
	{
		if (NewMax > UINT32_MAX / 2)
		{
			NewMax = RequiredCount;
			break;
		}
		NewMax *= 2;
	}
	return Create(InDevice, NewMax);
}

bool FDynamicIndexBuffer::Update(ID3D11DeviceContext* Context, const void* Data, uint32 Count)
{
	if (!Buffer || !Context || !Data || Count == 0) return false;
	if (Count > MaxCount) return false;
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(Context->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped))) return false;
	memcpy(Mapped.pData, Data, sizeof(uint32) * Count);
	Context->Unmap(Buffer, 0);
	return true;
}

void FDynamicIndexBuffer::Bind(ID3D11DeviceContext* Context)
{
	Context->IASetIndexBuffer(Buffer, DXGI_FORMAT_R32_UINT, 0);
}
