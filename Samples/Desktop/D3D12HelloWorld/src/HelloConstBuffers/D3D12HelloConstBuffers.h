//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "DXSample.h"
#include <vector>

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

#define USE_NORMALS_AND_TEXCOORDS 1
#define USE_MSAA 1

struct ObjVert {
	float x, y, z;
};

struct ObjTexCoord {
	float u, v;
};

struct ObjVertNorm {
	float x, y, z;
};

struct ObjVertBundle {
	ObjVert vertex;
	ObjTexCoord texCoord;
	ObjVertNorm normal;
};

struct ObjFace {
	std::vector<UINT> vertices;
};


struct MeshAsset {
	std::string path;
	std::vector<ObjVertBundle>* m_verts;
	std::vector<ObjFace>* m_faces;
	float xmin, xmax, ymin, ymax, zmin, zmax;
	float xmean, ymean, zmean;
};
struct Vertex
{
	XMFLOAT3 position;
	XMFLOAT4 color;
#if USE_NORMALS_AND_TEXCOORDS
	XMFLOAT3 normal;
	XMFLOAT2 texCoord;
#endif
};
struct Mesh {
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	D3D12_SUBRESOURCE_DATA m_vertexBufferSubresourceData;
	Vertex *m_cpuVertexBuffer;

	ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
	D3D12_SUBRESOURCE_DATA m_indexBufferSubresourceData;
	UINT *m_cpuIndexBuffer;

	UINT m_vertexCount;
	UINT m_indexCount;
};

class D3D12HelloConstBuffers : public DXSample
{
public:
	D3D12HelloConstBuffers(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

	static const UINT FrameCount = 2;



	struct SceneConstantBuffer
	{
		XMMATRIX model;
		XMMATRIX projection;
		XMFLOAT3 lightpos;
	};

	// Pipeline objects.
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12PipelineState> m_tonemapPSO;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12Resource> m_textureUploadHeap;
	UINT m_rtvDescriptorSize;
	float m_angle;

	const int kSampleCount = 8;
	const int kMipLevels = 3;

	// App resources.
	//we theorize this can be shared between meshes? right guys?

	ComPtr<ID3D12Resource> m_vertexBufferUploadHeap;
	ComPtr<ID3D12Resource> m_indexBufferUploadHeap;
#if USE_NORMALS_AND_TEXCOORDS
	ComPtr<ID3D12Resource> m_texture;
#endif
#if USE_MSAA
	ComPtr<ID3D12Resource> m_texturemsaa;
	ComPtr<ID3D12Resource> m_textureresolve;
#endif

	ComPtr<ID3D12Resource> m_constantBuffer;
	SceneConstantBuffer m_constantBufferData;
	UINT8* m_pCbvDataBegin;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;
	UINT m_cbvSrvDescriptorSize;

	CD3DX12_CPU_DESCRIPTOR_HANDLE m_cbvSrvHandle;	// Move past the SRV in slot 1.

	std::vector<Mesh> m_meshes;
	std::vector<MeshAsset> m_meshassets;


	void LoadPipeline();
	void LoadAssets();

	void CreateConstantBuffer();
	void CreateTextureResource();
	void CreateMesh(std::string objname);
	void CreateRTs();

	void PopulateCommandList();
	void WaitForPreviousFrame();
};
