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

cbuffer SceneConstantBuffer : register(b0)
{
	float4x4 model;
	float4x4 projection;
	float3 lightpos;
};


struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
	float4 normal : NORMAL;
	float4 texCoord : TEXCOORD0;
	float4 worldpos : TEXCOORD1;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR, float4 normal : NORMAL, float4 texCoord : TEXCOORD)
{
	PSInput result;

	float4x4 t_proj = transpose(projection);
	float4x4 t_mod = transpose(model);
	float4x4 mat = mul(t_proj, t_mod);

	result.position = mul(mat, position);
	result.position /= result.position.w;
	color.w = 1.0;
	result.color = color;
	result.normal = mul(t_mod, float4(normal.xyz, 0));
	result.texCoord = texCoord;
	result.worldpos = mul(t_mod, position);

	return result;
}

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 lightdir = normalize(lightpos - input.worldpos);
	return g_texture.Sample(g_sampler, input.texCoord) * mul(input.normal, lightdir);
}
