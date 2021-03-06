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

#include "stdafx.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "D3D12HelloConstBuffers.h"
#include "ImageLoader.h"

#include <vector>
#include <fstream>
#include <string>
#include <sstream>
using std::vector;
using std::string;
using std::istringstream;
using Gdiplus::Bitmap;
using Gdiplus::Color;

typedef D3D12HelloConstBuffers::Vertex Vertex;


D3D12HelloConstBuffers::D3D12HelloConstBuffers(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_pCbvDataBegin(nullptr),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0),
    m_constantBufferData{},
    m_angle(0.0f)

{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    // Initialize GDI+.
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

void D3D12HelloConstBuffers::OnInit()
{
    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12HelloConstBuffers::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }

    m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
        ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount + 1; // 1 backbuffer per frame + 1 MSAA RT
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Describe and create a constant buffer view (CBV) descriptor heap.
        // Flags indicate that this descriptor heap can be bound to the pipeline 
        // and that descriptors contained in it can be referenced by a root table.
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
        cbvHeapDesc.NumDescriptors = 2;
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));
    }

    CreateRTs(); // Just MSAA-related textures for now

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
#if USE_MSAA
        D3D12_RENDER_TARGET_VIEW_DESC  rtvDescMSAA = {};
        rtvDescMSAA.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        rtvDescMSAA.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        m_device->CreateRenderTargetView(m_texturemsaa.Get(), &rtvDescMSAA, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
#endif
    }



    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

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
    vector<ObjVertBundle> vertices;
};

Vertex vertBundleToVert(ObjVertBundle bundle) {
    Vertex v;
    v.position.x = bundle.vertex.x;
    v.position.y = bundle.vertex.y;
    v.position.z = bundle.vertex.z;

    v.color.x = 0.f;
    v.color.y = 0.f;
    v.color.z = 0.f;
    v.color.w = 0.f;
#if USE_NORMALS_AND_TEXCOORDS
    v.normal.x = bundle.normal.x;
    v.normal.y = bundle.normal.y;
    v.normal.z = bundle.normal.z;

    v.texCoord.x = bundle.texCoord.u;
    v.texCoord.y = bundle.texCoord.v;
#endif
    return v;
}

void objToBuffers(vector<ObjFace> faces, Vertex** vb, short** ib, UINT& vbSize, UINT& ibSize) {
    vector<Vertex> vertices;

    for (int i = 0; i < faces.size(); ++i) {
        ObjFace face = faces[i];

        for (int j = 1; j < face.vertices.size() - 1; ++j) {
            vertices.push_back(vertBundleToVert(face.vertices[0]));
            vertices.push_back(vertBundleToVert(face.vertices[j]));
            vertices.push_back(vertBundleToVert(face.vertices[j + 1]));
        }
    }

    *vb = new Vertex[vertices.size()];
    *ib = new short[vertices.size()];

    for (short i = 0; i < vertices.size(); ++i) {
        (*vb)[i] = vertices[i];
        (*ib)[i] = i;
    }

    vbSize = vertices.size() * sizeof(Vertex);
    ibSize = vertices.size() * sizeof(short);
}

vector<ObjFace> parseOBJ(string fname) {
    using namespace std;
    ifstream file(fname);
    string str;

    vector<ObjVert> vertices;
    vector<ObjTexCoord> texes;
    vector<ObjVertNorm> normals;
    vector<ObjVertBundle> bundles;
    vector<ObjFace> faces;

    while (getline(file, str)) {
        if (str[0] == '#') {
            continue;
        }

        istringstream iss(str);
        string prefix;
        iss >> prefix;

        if (prefix == "v") {
            ObjVert vert;
            iss >> vert.x;
            iss >> vert.y;
            iss >> vert.z;
            vertices.push_back(vert);
        }
        else if (prefix == "vt") {
            ObjTexCoord tex;
            iss >> tex.u;
            iss >> tex.v;
            texes.push_back(tex);
        }
        else if (prefix == "vn") {
            ObjVertNorm normal;
            iss >> normal.x;
            iss >> normal.y;
            iss >> normal.z;
            normals.push_back(normal);
        }
        else if (prefix == "f") {
            ObjFace face;
            string vertBundleString;
            for (int i = 0; i < 5; ++i) {
                iss >> vertBundleString;

                stringstream vertBundleStream(vertBundleString);
                string segment;
                std::vector<string> seglist;

                while (getline(vertBundleStream, segment, '/')) {
                    seglist.push_back(segment);
                }

                int vertIdx = stoi(seglist[0]) - 1;
                int texIdx = stoi(seglist[1]) - 1;
                int normIdx = stoi(seglist[2]) - 1;

                face.vertices.push_back(
                    ObjVertBundle{
                        vertices[vertIdx],
                        texes[texIdx],
                        normals[normIdx]
                    }
                );
            }
            faces.push_back(face);
        }
    }

    return faces;
}

// Load the sample assets.
void D3D12HelloConstBuffers::LoadAssets()
{
    // Create a root signature consisting of a descriptor table with a single CBV.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

#if USE_NORMALS_AND_TEXCOORDS
#define NUM_ROOT_DESCRIPTORS 2
#else
#define NUM_ROOT_DESCRIPTORS 1
#endif 
        CD3DX12_DESCRIPTOR_RANGE1 ranges[NUM_ROOT_DESCRIPTORS];
        CD3DX12_ROOT_PARAMETER1 rootParameters[NUM_ROOT_DESCRIPTORS];

        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);

#if USE_NORMALS_AND_TEXCOORDS
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
#endif


        // Allow input layout and deny uneccessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS/* |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS*/;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
#if USE_NORMALS_AND_TEXCOORDS
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, rootSignatureFlags);
#else 
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);
#endif 

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> error;
        char *error2;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error);
        error2 = (char*)error->GetBufferPointer();
        D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error);
        error2 = (char*)error->GetBufferPointer();

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
#if USE_NORMALS_AND_TEXCOORDS
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
#endif
        };
        CD3DX12_RASTERIZER_DESC rasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
#if USE_MSAA
        rasterizerDesc.MultisampleEnable = TRUE;
#endif

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = rasterizerDesc;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

#if USE_MSAA
        psoDesc.SampleDesc.Count = kSampleCount;
        psoDesc.SampleDesc.Quality = DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN;
#else
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;
#endif
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
// // Tonemapping later
//#if USE_MSAA
//		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
//		psoDesc.pRootSignature = m_rootSignature.Get();
//		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
//		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
//		psoDesc.RasterizerState = rasterizerDesc;
//		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
//		psoDesc.DepthStencilState.DepthEnable = FALSE;
//		psoDesc.DepthStencilState.StencilEnable = FALSE;
//		psoDesc.SampleMask = UINT_MAX;
//		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
//		psoDesc.NumRenderTargets = 1;
//		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
//		psoDesc.SampleDesc.Count = kSampleCount;
//		psoDesc.SampleDesc.Quality = DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN;
//		//ComPtr<ID3DBlob> resolveVert; 
//		//ComPtr<ID3DBlob> resolveFrag;
//#endif
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));


    CreateTextureResource();


    // Create the vertex buffer.
    {	
        UINT vertexBufferSize;
        UINT indexBufferSize;
        Vertex* triangleVertices;
        short* indices;

        vector<ObjFace> faces = parseOBJ("..\\..\\..\\..\\..\\Resources\\dodecahedron.obj");

        objToBuffers(faces, &triangleVertices, &indices, vertexBufferSize, indexBufferSize);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBufferUploadHeap)));

        m_vertexBufferDataFuck = {};
        m_vertexBufferDataFuck.pData = (BYTE*)triangleVertices;
        m_vertexBufferDataFuck.RowPitch = vertexBufferSize;
        m_vertexBufferDataFuck.SlicePitch = vertexBufferSize;
        // Copy the triangle data to the vertex buffer.
        UpdateSubresources<1>(m_commandList.Get(), m_vertexBuffer.Get(), m_vertexBufferUploadHeap.Get(), 0, 0, 1, &m_vertexBufferDataFuck);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    CreateConstantBuffer();

    ThrowIfFailed(m_commandList->Close());

    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForPreviousFrame();
    }
}

// Update frame-based values.
void D3D12HelloConstBuffers::OnUpdate()
{
    const float translationSpeed = 0.005f;
    const float offsetBounds = 1.25f;

    m_angle += 0.005f;

    m_constantBufferData.model.r[0] = { cos(m_angle), 0.0f, -sin(m_angle), 0.0f };
    m_constantBufferData.model.r[1] = { 0.0f,         1.0f, 0.0f,          0.0f };
    m_constantBufferData.model.r[2] = { sin(m_angle), 0.0f, cos(m_angle),  2.0f };
    m_constantBufferData.model.r[3] = { 0.0f,         0.0f, 0.0f,          1.0f };	



    m_constantBufferData.projection.r[0] = { 1.0f, 0.0f, 0.0f, 0.0f };
    m_constantBufferData.projection.r[1] = { 0.0f, m_aspectRatio, 0.0f, 0.0f };
    m_constantBufferData.projection.r[2] = { 0.0f, 0.0f, 1.0f, 0.0f };
    m_constantBufferData.projection.r[3] = { 0.0f, 0.0f, 1.0f, 1.0f };

    m_constantBufferData.lightpos = { 1.0f, 1.0f, 0.0f };

    memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
}

// Render the scene.
void D3D12HelloConstBuffers::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));

    WaitForPreviousFrame();
}

void D3D12HelloConstBuffers::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_fenceEvent);
}

// Fill the command list with all the render commands and dependent state.
void D3D12HelloConstBuffers::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    auto handle = m_cbvHeap->GetGPUDescriptorHandleForHeapStart();
    m_commandList->SetGraphicsRootDescriptorTable(0, handle);
    auto new_handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(handle, m_cbvSrvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(1, new_handle);

    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    // Indicate that the MSAA buffer will be used as a render target
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
#if USE_MSAA
        m_texturemsaa.Get(),
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
#else
        m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
#endif
        D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
#if USE_MSAA
        2,
#else
        m_frameIndex,
#endif
        m_rtvDescriptorSize); // 0 and 1 are the per-frame backbuffers; 2 is the MSAA RT
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr); // Set MSAA texture as render target

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->DrawInstanced(m_vertexBufferView.SizeInBytes / sizeof(Vertex), 1, 0, 0);

    // Indicate that the back buffer will now be used to present.
#if USE_MSAA
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texturemsaa.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST));
    m_commandList->ResolveSubresource(m_renderTargets[m_frameIndex].Get(), D3D12CalcSubresource(0, 0, 0, 1, 1), m_texturemsaa.Get(), D3D12CalcSubresource(0, 0, 0, 1, 1), DXGI_FORMAT_R8G8B8A8_UNORM);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PRESENT));
#else
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
#endif

    ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloConstBuffers::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}


void D3D12HelloConstBuffers::CreateConstantBuffer()
{
    // Create the constant buffer.
    {
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(1024 * 64),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_constantBuffer)));

        // Describe and create a constant buffer view.
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = (sizeof(SceneConstantBuffer) + 255) & ~255;	// CB size is required to be 256-byte aligned.
        m_device->CreateConstantBufferView(&cbvDesc, m_cbvSrvHandle);

        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin)));
        memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
    }

}

//
// Align uLocation to the next multiple of uAlign.
//

UINT Align(UINT uLocation, UINT uAlign)
{
    if ((0 == uAlign) || (uAlign & (uAlign - 1)))
    {
        //ThrowException("non-pow2 alignment");
        throw HrException(0);

    }

    return ((uLocation + (uAlign - 1)) & ~(uAlign - 1));
}

void D3D12HelloConstBuffers::CreateTextureResource()
{
#if USE_NORMALS_AND_TEXCOORDS

    {
    auto loaded = ImageLoader(L"..\\..\\..\\..\\..\\Resources\\dodecahedron.bmp");
    MipMap mip0 = loaded.getMipMap(0);


        // Create the texture.
        // Describe and create a Texture2D.
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = kMipLevels;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.Width = mip0.width;
        textureDesc.Height = mip0.height;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_texture)));

        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, kMipLevels)*10;

        // Create the GPU upload buffer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_textureUploadHeap)));
        void *pUploadHeapData;
        CD3DX12_RANGE range(0, 0);
        m_textureUploadHeap->Map(0, &range, &pUploadHeapData);

        int w = mip0.width;
        int h = mip0.height;

        int bytes_written = 0;
        for (int i = 0; i < kMipLevels; i++)
        {
            auto this_mip = loaded.getMipMap(i);

            /*D3D12_SUBRESOURCE_DATA textureData = {};
            textureData.pData = &mytexturedata[0];
            textureData.RowPitch = this_w;
            textureData.SlicePitch = this_h;*/

            D3D12_SUBRESOURCE_FOOTPRINT pitchedDesc = { 0 };
            pitchedDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            pitchedDesc.Width = this_mip.width;
            pitchedDesc.Height = this_mip.height;
            pitchedDesc.Depth = 1;
            pitchedDesc.RowPitch = Align(this_mip.width * sizeof(DWORD), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

            D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedTexture2D = { 0 };
            placedTexture2D.Offset = bytes_written;
            placedTexture2D.Footprint = pitchedDesc;

            int towrite = this_mip.width * this_mip.height * 4;
            memcpy((char*)pUploadHeapData + bytes_written, this_mip.bytes, towrite);
            bytes_written += towrite;

            int subresource = D3D12CalcSubresource(i, 0, 0, kMipLevels, 1);

            CD3DX12_TEXTURE_COPY_LOCATION dstLoc(m_texture.Get(), subresource);
            CD3DX12_TEXTURE_COPY_LOCATION srcLoc(m_textureUploadHeap.Get(), placedTexture2D);
            //CD3DX12_BOX box(0, 0, 0, w / factor, h / factor, 1);
            m_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);//&box);

        }

        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        // Describe and create a SRV for the texture.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = kMipLevels;
        srvDesc.Texture2D.PlaneSlice = 0;
        auto orig_handle = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
        m_cbvSrvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(orig_handle, 0, m_cbvSrvDescriptorSize);
        m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_cbvSrvHandle);
        m_cbvSrvHandle.Offset(1, m_cbvSrvDescriptorSize);


        // Re-add this for HDR
        //D3D12_SHADER_RESOURCE_VIEW_DESC srvResolveDesc = {};
        //srvResolveDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        //srvResolveDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // DXGI_FORMAT_R10G10B10A2_UNORM;
        //srvResolveDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        //srvResolveDesc.Texture2D.MipLevels = 1;
        //m_device->CreateShaderResourceView(m_textureresolve.Get(), &srvResolveDesc, m_cbvSrvHandle);
        //m_cbvSrvHandle.Offset(1, m_cbvSrvDescriptorSize);
    }
#endif
}

void D3D12HelloConstBuffers::CreateRTs() 
{
#if USE_MSAA
    // Create the texture.
    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDescMSAA = {};
    textureDescMSAA.MipLevels = 1;
    textureDescMSAA.Format = DXGI_FORMAT_R8G8B8A8_UNORM; //DXGI_FORMAT_R10G10B10A2_UNORM; // HDR :3c
    textureDescMSAA.Width = m_width;
    textureDescMSAA.Height = m_height;
    textureDescMSAA.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    textureDescMSAA.DepthOrArraySize = 1;
    textureDescMSAA.SampleDesc.Count = kSampleCount;
    textureDescMSAA.SampleDesc.Quality = DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN;
    textureDescMSAA.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    
    // Assign an optimized clear value; more efficient
    float clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
    CD3DX12_CLEAR_VALUE clearValue(DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM, clearColor);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDescMSAA,
        D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
        &clearValue,
        IID_PPV_ARGS(&m_texturemsaa)));


    D3D12_RESOURCE_DESC textureDescResolve = {};
    textureDescResolve.MipLevels = 1;
    textureDescResolve.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // DXGI_FORMAT_R10G10B10A2_UNORM; // HDR :3c
    textureDescResolve.Width = m_width;
    textureDescResolve.Height = m_height;
    textureDescResolve.Flags = D3D12_RESOURCE_FLAG_NONE;
    textureDescResolve.DepthOrArraySize = 1;
    textureDescResolve.SampleDesc.Count = 1;
    textureDescResolve.SampleDesc.Quality = 0;
    textureDescResolve.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;


    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDescResolve,
        D3D12_RESOURCE_STATE_RESOLVE_DEST,
        nullptr,
        IID_PPV_ARGS(&m_textureresolve)));
#endif
}
