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

static const float n1 = 1.0f;
static const float n2 = 1.1f;
static const float pi = 3.14159265f;

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


float getTheta(float4 viewVector, float4 normal)
{
	return acos(mul(viewVector, normal));
}

float getThetaT(float theta)
{
	return asin((n1 * sin(theta)) / n2);
}

float rPerpendicular(float theta) {
	float thetaT = getThetaT(theta);
	return (n1 * cos(theta) - n2 * cos(thetaT)) / (n1 * cos(theta) + n2 * cos(thetaT));
}

float rParallel(float theta) {
	float thetaT = getThetaT(theta);
	return (n2 * cos(theta) - n1 * cos(thetaT)) / (n2 * cos(theta) + n1 * cos(thetaT));
}

float ggx_ndf(float3 h, float3 n, float alphasquared)
{
	float ret = 1.0;
	float ndoth = mul(n, h);
	if (ndoth <= 0)
		return 0;

	ret *= ndoth;

	ret *= alphasquared;

	float alphasquared_minus1 = (alphasquared - 1);

	float denom = pi * (1 + (ndoth*ndoth)*(alphasquared_minus1*alphasquared_minus1));

	ret /= denom;
	return ret;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 result;

	// Diffuse
	float3 lightdir = normalize(lightpos - input.worldpos);
	result = g_texture.Sample(g_sampler, input.texCoord) *mul(input.normal, lightdir);

	float4 viewVector = -normalize(input.worldpos);

	float3 h = normalize(viewVector + lightdir);
	float alpha = 0.4;
	float ndf_value = ggx_ndf(h, input.normal, alpha*alpha);

	float ndotl = mul(input.normal, lightdir);
	float ndotv = mul(input.normal, viewVector);

	float masking_shadowing = 0.5 / lerp(2 * ndotl*ndotv, ndotl + ndotv, alpha);
	float F_0 = 0.5;
	float hdotlplus = max(0, mul(h, lightdir));
	float fresnel = F_0 + (1 - F_0)*pow((1 - hdotlplus), 5);

	result += fresnel * masking_shadowing * ndf_value;

	// Fresnel
	float theta = getTheta(viewVector, input.normal);
	float rPerp = rPerpendicular(theta);
	float rPar = rParallel(theta);
	float reflectance = (rPerp * rPerp + rPar * rPar) / 2.0f;
	//result += reflectance * float4(0.8f, 0.0f, 0.8f, 0.0f);

	return result;
}