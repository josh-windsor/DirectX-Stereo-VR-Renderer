#include "Framework.h"

#include "ShaderSet.h"
#include "Mesh.h"
#include "Texture.h"
#include <OVR_CAPI.h>

using namespace DirectX;

//================================================================================
// Normal Mapping Application
// An example of how to work with normal maps.
//================================================================================
class NormalMappingApp : public FrameworkApp
{
public:

	struct PerFrameCBData
	{
		m4x4 m_matProjection;
		m4x4 m_matView;
		v4   m_lightPos;
		f32  m_time;
		f32  m_padding[3];
	};

	struct PerDrawCBData
	{
		m4x4 m_matMVP;
		m4x4 m_matWorld;
		v4   m_matNormal[3]; // because of structure packing rules this represents a float3x3 in HLSL.
		m4x4 m_modelViewProj[2];
		UINT m_tileFactor;
		UINT m_padding[3];

	};

	void on_init(SystemsInterface& systems) override
	{
		m_position = v3(0.5f, 0.5f, 0.5f);
		m_size = 1.0f;
		systems.pCamera->eye = v3(3.f, 1.5f, 3.f);
		systems.pCamera->look_at(v3(3.f, 1.5f, 0.f));

		// compile a set of shaders
		m_meshShader[0].init(systems.pD3DDevice
			, ShaderSetDesc::Create_VS_PS("Assets/Shaders/NormalMappingShaders.fx", "VS_Mesh", "PS_Mesh")
			, { VertexFormatTraits<MeshVertex>::desc, VertexFormatTraits<MeshVertex>::size }
		);
		m_meshShader[1].init(systems.pD3DDevice
			, ShaderSetDesc::Create_VS_PS("Assets/Shaders/NormalMappingShaders.fx", "VS_Mesh_Instanced", "PS_Mesh")
			, { VertexFormatTraits<MeshVertex>::desc, VertexFormatTraits<MeshVertex>::size }
		);

		// Create Per Frame Constant Buffer.
		m_pPerFrameCB = create_constant_buffer<PerFrameCBData>(systems.pD3DDevice);

		// Create Per Frame Constant Buffer.
		m_pPerDrawCB = create_constant_buffer<PerDrawCBData>(systems.pD3DDevice);

		// Initialize a mesh directly.
		create_mesh_cube(systems.pD3DDevice, m_meshArray[0], 0.5f);

		// Initialize a mesh from an .OBJ file
		create_mesh_from_obj(systems.pD3DDevice, m_meshArray[1], "Assets/Models/WoodCrate/wc1.obj", 1.f);
		create_mesh_from_obj(systems.pD3DDevice, m_meshArray[2], "Assets/Models/Plane/plane.obj", 2.f);
		create_mesh_from_obj(systems.pD3DDevice, m_meshArray[3], "Assets/Models/House/house.obj", 0.006f);
		create_mesh_from_obj(systems.pD3DDevice, m_meshArray[4], "Assets/Models/Bus/bus.obj", 0.1f);
		create_mesh_from_obj(systems.pD3DDevice, m_meshArray[5], "Assets/Models/House2/house2.obj", 1.f);

		// Initialise some textures;
		m_textures[0].init_from_dds(systems.pD3DDevice, "Assets/Models/WoodCrate/wc1_diffuse.dds");
		m_textures[1].init_from_dds(systems.pD3DDevice, "Assets/Models/WoodCrate/wc1_normal.dds");
		m_textures[2].init_from_dds(systems.pD3DDevice, "Assets/Models/Plane/brick_diffuse.dds");
		m_textures[3].init_from_dds(systems.pD3DDevice, "Assets/Models/Plane/brick_normal.dds");
		m_textures[4].init_from_dds(systems.pD3DDevice, "Assets/Models/House/house_diffuse.dds");
		m_textures[5].init_from_dds(systems.pD3DDevice, "Assets/Models/House/house_normal.dds");
		m_textures[6].init_from_dds(systems.pD3DDevice, "Assets/Models/Bus/bus_diffuse.dds");
		m_textures[7].init_from_dds(systems.pD3DDevice, "Assets/Models/Bus/bus_normal.dds");
		m_textures[8].init_from_dds(systems.pD3DDevice, "Assets/Models/House2/house2_diffuse.dds");
		m_textures[9].init_from_dds(systems.pD3DDevice, "Assets/Models/House2/house2_normal.dds");

		// We need a sampler state to define wrapping and mipmap parameters.
		m_pLinearMipSamplerState = create_basic_sampler(systems.pD3DDevice, D3D11_TEXTURE_ADDRESS_WRAP);

		// Setup per-frame data
		m_perFrameCBData.m_time = 0.0f;
	}

	void on_update(SystemsInterface& systems) override
	{
		//////////////////////////////////////////////////////////////////////////
		// You can use features from the ImGui library.
		// Investigate the ImGui::ShowDemoWindow() function for ideas.
		// see also : https://github.com/ocornut/imgui
		//////////////////////////////////////////////////////////////////////////

		// This function displays some useful debugging values, camera positions etc.
		DemoFeatures::editorHud(systems.pDebugDrawContext);


	}

	//function to clear oculus stuff
	void SetAndClearRenderTarget(ID3D11RenderTargetView * rendertarget, ID3D11DepthStencilView* depthStencil, ID3D11DeviceContext* context)
	{
		//Set & Clear buffers
		f32 clearValue[] = { 0.f, 0.f, 0.f, 0.f };

		context->OMSetRenderTargets(1, &rendertarget, depthStencil);
		context->ClearRenderTargetView(rendertarget, clearValue);
		if (depthStencil)
			context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
	}

	//create viewport easily
	void SetViewport(SystemsInterface& systems, float vpX, float vpY, float vpW, float vpH)
	{
		D3D11_VIEWPORT D3Dvp;
		D3Dvp.Width = vpW;    D3Dvp.Height = vpH;
		D3Dvp.MinDepth = 0;   D3Dvp.MaxDepth = 1;
		D3Dvp.TopLeftX = vpX; D3Dvp.TopLeftY = vpY;
		systems.pD3DContext->RSSetViewports(1, &D3Dvp);
	}

	//draws a single model based on the passed in params
	void DrawSingleModel(ID3D11DeviceContext* pContext, bool renderStereo, XMMATRIX* prod, int mesh, int texture, v3 translation, int yRot, int tileFactor)
	{
		m_meshArray[mesh].bind(pContext);
		m_textures[texture].bind(pContext, ShaderStage::kPixel, 0);
		m_textures[texture + 1].bind(pContext, ShaderStage::kPixel, 1);


		m4x4 matWorld = m4x4::CreateTranslation(translation) * m4x4::CreateRotationY(degToRad(yRot));
		if (renderStereo)
		{
			//sets both viewproj matrix for stereo offset
			m_perDrawCBData.m_modelViewProj[0] = (matWorld * prod[0]).Transpose();
			m_perDrawCBData.m_modelViewProj[1] = (matWorld * prod[1]).Transpose();
		}
		else
		{
			//calculate main MVP matrix
			m4x4 matMVP = matWorld * *prod;
			m_perDrawCBData.m_matMVP = matMVP.Transpose();
		}

		// Update Per Draw Data
		m_perDrawCBData.m_matWorld = matWorld.Transpose();
		m_perDrawCBData.m_tileFactor = tileFactor;

		//pack the normals into world
		pack_upper_float3x3(m_perDrawCBData.m_matWorld, m_perDrawCBData.m_matNormal);

		// Push to GPU
		push_constant_buffer(pContext, m_pPerDrawCB, m_perDrawCBData);

		// Draw the mesh.
		if (renderStereo)
		{
			m_meshArray[mesh].drawIndexedInstanced(pContext);
		}
		else
		{
			m_meshArray[mesh].draw(pContext);
		}

	}


	//render the scene to the headset
	//prod will be a single viewproj matrix for each eye or both for stereo
	void RenderScene(SystemsInterface& systems, XMMATRIX* prod, bool renderStereo)
	{
		// Update Per Frame Data.
		m_perFrameCBData.m_matProjection = XMMatrixTranspose(*prod);
		m_perFrameCBData.m_matView = XMMatrixTranspose(*prod);
		m_perFrameCBData.m_time += 0.001f;
		m_perFrameCBData.m_lightPos = v4(sin(m_perFrameCBData.m_time*5.0f) * 4.f + 3.0f, 1.f, 2.f, 0.f);


		// Push Per Frame Data to GPU
		push_constant_buffer(systems.pD3DContext, m_pPerFrameCB, m_perFrameCBData);

		// Bind our set of shaders depending on if stereo is running
		m_meshShader[renderStereo].bind(systems.pD3DContext);

		// Bind Constant Buffers, to both PS and VS stages
		ID3D11Buffer* buffers[] = { m_pPerFrameCB, m_pPerDrawCB };
		systems.pD3DContext->VSSetConstantBuffers(0, 2, buffers);
		systems.pD3DContext->PSSetConstantBuffers(0, 2, buffers);

		// Bind a sampler state
		ID3D11SamplerState* samplers[] = { m_pLinearMipSamplerState };
		systems.pD3DContext->PSSetSamplers(0, 1, samplers);

		constexpr f32 kGridSpacing = 1.5f;
		constexpr u32 kNumInstances = 5;
		constexpr u32 kNumModelTypes = 2;

		for (u32 i = 0; i < kNumModelTypes; ++i)
		{
			// Bind a mesh and texture.
			m_meshArray[i].bind(systems.pD3DContext);
			m_textures[0].bind(systems.pD3DContext, ShaderStage::kPixel, 0);
			m_textures[1].bind(systems.pD3DContext, ShaderStage::kPixel, 1);

			// Draw several instances
			for (u32 j = 0; j < kNumInstances; ++j)
			{
				// Compute MVP matrix.
				m4x4 matWorld = m4x4::CreateTranslation(v3(j * kGridSpacing, i * kGridSpacing, -3.f));
				if (renderStereo)
				{
					m_perDrawCBData.m_modelViewProj[0] = (matWorld * prod[0]).Transpose();
					m_perDrawCBData.m_modelViewProj[1] = (matWorld * prod[1]).Transpose();
				}
				else
				{
					m4x4 matMVP = matWorld * *prod;
					m_perDrawCBData.m_matMVP = matMVP.Transpose();
				}

				// Update Per Draw Data
				m_perDrawCBData.m_matWorld = matWorld.Transpose();
				m_perDrawCBData.m_tileFactor = 1;

				// Inverse transpose,  but since we didn't do any shearing or non-uniform scaling then we simple grab the upper 3x3 in the shader.
				pack_upper_float3x3(m_perDrawCBData.m_matWorld, m_perDrawCBData.m_matNormal);

				// Push to GPU
				push_constant_buffer(systems.pD3DContext, m_pPerDrawCB, m_perDrawCBData);

				// Draw the mesh.
				if (renderStereo)
				{
					m_meshArray[i].drawIndexedInstanced(systems.pD3DContext);
				}
				else
				{
					m_meshArray[i].draw(systems.pD3DContext);
				}

			}
		}

		//Floor
		DrawSingleModel(systems.pD3DContext, renderStereo, prod, 2, 2, v3(0.f, -0.5f, 0.f), 0, 9);
		//house
		DrawSingleModel(systems.pD3DContext, renderStereo, prod, 3, 4, v3(4.f, -0.5f, 2.f), -90, 1);
		//bus
		DrawSingleModel(systems.pD3DContext, renderStereo, prod, 4, 6, v3(7.f, -0.5f, 2.f), 0, 1);
		//house2
		DrawSingleModel(systems.pD3DContext, renderStereo, prod, 5, 8, v3(-2.f, -0.5f, 11.f), 180, 1);
	}

	void on_render(SystemsInterface& systems) override
	{
		//get the current oculus session
		ovrSessionStatus sessionStatus;
		ovrResult result = ovr_GetSessionStatus(*systems.pOvrSession, &sessionStatus);
		printf(std::to_string(result).c_str());
		if (OVR_FAILURE(result))
			panicF("Connection failed.");

		//VR Implementation 
		ovrHmdDesc hmdDesc = ovr_GetHmdDesc(*systems.pOvrSession);

		// Call ovr_GetRenderDesc each frame to get the ovrEyeRenderDesc, as the returned values (e.g. HmdToEyePose) may change at runtime.
		ovrEyeRenderDesc eyeRenderDesc[2];
		eyeRenderDesc[0] = ovr_GetRenderDesc(*systems.pOvrSession, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
		eyeRenderDesc[1] = ovr_GetRenderDesc(*systems.pOvrSession, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);

		// Get both eye poses simultaneously, with IPD offset already included. 
		ovrPosef EyeRenderPose[2];
		ovrPosef HmdToEyePose[2] = { eyeRenderDesc[0].HmdToEyePose,
									 eyeRenderDesc[1].HmdToEyePose };

		double sensorSampleTime;    // sensorSampleTime is fed into the layer later
		//get the eye poses
		ovr_GetEyePoses(*systems.pOvrSession, 0, ovrTrue, HmdToEyePose, EyeRenderPose, &sensorSampleTime);

		ovrTimewarpProjectionDesc posTimewarpProjectionDesc = {};

		//stores the view matricies for switching between instanced & double render
		XMMATRIX viewProjMatrix[2];
		XMMATRIX projMatrix[2];

		SetAndClearRenderTarget(systems.pEyeRenderTexture->GetRTV(), systems.pEyeRenderTexture->GetDSV(), systems.pD3DContext);

		// Render Scene to Eye Buffers
		for (int eye = 0; eye < 2; ++eye)
		{

			//Get the pose information from the rift in XM format
			XMVECTOR eyeQuat = XMLoadFloat4((XMFLOAT4 *)&EyeRenderPose[eye].Orientation.x);
			XMVECTOR eyePos = XMVectorSet(EyeRenderPose[eye].Position.x, EyeRenderPose[eye].Position.y, EyeRenderPose[eye].Position.z, 0);

			// Get view and projection matrices for the Rift camera
			SimpleMath::Quaternion camRot;
			m4x4::Transform(systems.pCamera->viewMatrix, camRot);
			//calculate combined vectors of the main camera and oculus eyes
			XMVECTOR combinedPos = XMVectorAdd(XMLoadFloat3(&systems.pCamera->eye), XMVector3Rotate(eyePos, camRot));
			XMVECTOR combinedRot = XMQuaternionMultiply(eyeQuat, camRot);
			//create a camera for each eye
			Camera finalCam;
			finalCam.eye = combinedPos;
			//rotate by the main camera
			finalCam.forward = XMVector3Rotate(finalCam.forward, combinedRot);
			finalCam.up = XMVector3Rotate(finalCam.up, combinedRot);
			finalCam.right = XMVector3Rotate(finalCam.right, combinedRot);
			finalCam.updateMatrices();
			//generate oculus view and projection matrix
			XMMATRIX view = finalCam.viewMatrix;
			ovrMatrix4f p = ovrMatrix4f_Projection(eyeRenderDesc[eye].Fov, 0.2f, 1000.0f, ovrProjection_None);
			posTimewarpProjectionDesc = ovrTimewarpProjectionDesc_FromProjection(p, ovrProjection_None);
			//manually set projection matrix
			XMMATRIX proj = XMMatrixSet(p.M[0][0], p.M[1][0], p.M[2][0], p.M[3][0],
										p.M[0][1], p.M[1][1], p.M[2][1], p.M[3][1],
										p.M[0][2], p.M[1][2], p.M[2][2], p.M[3][2],
										p.M[0][3], p.M[1][3], p.M[2][3], p.M[3][3]);

			if (systems.stereo)
			{
				// scale and offset projection matrix to shift image to correct part of texture for each eye
				XMMATRIX scale = XMMatrixScaling(0.5f, 1.0f, 1.0f);
				XMMATRIX translate = XMMatrixTranslation((eye == 0) ? -0.5f : 0.5f, 0.0f, 0.0f);
				proj = XMMatrixMultiply(proj, scale);
				proj = XMMatrixMultiply(proj, translate);
			}

			//create the view projection matrix for application to models
			viewProjMatrix[eye] = XMMatrixMultiply(view, proj);
			projMatrix[eye] = view;

		}
		if (systems.stereo)
		{
			// use instancing for stereo
			//set viewport to be the length of both eyes
			SetViewport(systems, 0.0f, 0.0f, (float)systems.pEyeRenderViewport[0]->Size.w + systems.pEyeRenderViewport[1]->Size.w, (float)systems.pEyeRenderViewport[0]->Size.h);
			// Update Per Frame Data.
			m_perFrameCBData.m_matProjection = XMMatrixTranspose(projMatrix[0]);
			m_perFrameCBData.m_matView = XMMatrixTranspose(viewProjMatrix[0]);
			m_perFrameCBData.m_time += 0.001f;
			m_perFrameCBData.m_lightPos = v4(sin(m_perFrameCBData.m_time*5.0f) * 4.f + 3.0f, 1.f, 2.f, 0.f);

			// render scene
			RenderScene(systems, &viewProjMatrix[0], systems.stereo);

		}
		else
		{
			// non-instanced path
			for (int eye = 0; eye < 2; ++eye)
			{
				// set viewport for each eye individually
				SetViewport(systems, (float)systems.pEyeRenderViewport[eye]->Pos.x, (float)systems.pEyeRenderViewport[eye]->Pos.y,
					(float)systems.pEyeRenderViewport[eye]->Size.w, (float)systems.pEyeRenderViewport[eye]->Size.h);


				//render scene
				RenderScene(systems, &viewProjMatrix[eye], systems.stereo);
			}
		}
		// Commit rendering to the swap chain
		systems.pEyeRenderTexture->Commit();



		
		// Initialize our single full screen Fov layer.
		ovrLayerEyeFovDepth ld = {};
		ld.Header.Type = ovrLayerType_EyeFovDepth;
		ld.Header.Flags = 0;
		ld.ProjectionDesc = posTimewarpProjectionDesc;
		ld.SensorSampleTime = sensorSampleTime;

		//set final layer params to submit to headset
		for (int eye = 0; eye < 2; ++eye)
		{
			ld.ColorTexture[eye] = systems.pEyeRenderTexture->TextureChain;
			ld.DepthTexture[eye] = systems.pEyeRenderTexture->DepthTextureChain;
			ld.Viewport[eye] = *systems.pEyeRenderViewport[eye];
			ld.Fov[eye] = hmdDesc.DefaultEyeFov[eye];
			ld.RenderPose[eye] = EyeRenderPose[eye];
		}

		ovrLayerHeader* layers = &ld.Header;
		result = ovr_SubmitFrame(*systems.pOvrSession, 0, nullptr, &layers, 1);
		// exit the rendering loop if submit returns an error, will retry on ovrError_DisplayLost
		if (!OVR_SUCCESS(result))
			panicF("Fail Rendering Loop!");
	}



	void on_resize(SystemsInterface& ) override
	{
		
	}

private:

	PerFrameCBData m_perFrameCBData;
	ID3D11Buffer* m_pPerFrameCB = nullptr;

	PerDrawCBData m_perDrawCBData;
	ID3D11Buffer* m_pPerDrawCB = nullptr;

	ShaderSet m_meshShader[2];
	
	Mesh m_meshArray[6];
	Texture m_textures[10];
	ID3D11SamplerState* m_pLinearMipSamplerState = nullptr;

	v3 m_position;
	f32 m_size;
};

NormalMappingApp g_app;

FRAMEWORK_IMPLEMENT_MAIN(g_app, "Normal Maps")
