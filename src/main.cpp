#include <SDL3/SDL.h>
#include <vector>
#include <iostream>
#include <string>
#include <format>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <span>
#include <cstring>
#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <array>

int HEIGHT = 600;
int WIDTH = 800;
class SDLException final : public std::runtime_error {
public:
    explicit SDLException(const std::string &message) : std::runtime_error(message + "\n" + SDL_GetError()){

    }
};

static std::filesystem::path BasePath;

SDL_GPUShader* LoadShader(
	SDL_GPUDevice* device,
	const std::string &shaderFilename,
	const Uint32 samplerCount,
	const Uint32 uniformBufferCount,
	const Uint32 storageBufferCount,
	const Uint32 storageTextureCount
) {
	// Auto-detect the shader stage from the file name for convenience
	SDL_GPUShaderStage stage;
	if (shaderFilename.contains(".vert"))
	{
		stage = SDL_GPU_SHADERSTAGE_VERTEX;
	}
	else if (shaderFilename.contains(".frag"))
	{
		stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	}
	else
	{
		SDL_Log("Invalid shader stage!");
		return nullptr;
	}

	std::filesystem::path fullPath;
	const SDL_GPUShaderFormat backendFormats = SDL_GetGPUShaderFormats(device);
	SDL_GPUShaderFormat format{};
	std::string entrypoint = "main";

	if (backendFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
		fullPath = BasePath / "Content/Shaders/Compiled/SPIRV" / (shaderFilename + ".spv");
		format = SDL_GPU_SHADERFORMAT_SPIRV;
	} else if (backendFormats & SDL_GPU_SHADERFORMAT_MSL) {
		fullPath = BasePath / "Content/Shaders/Compiled/MSL" / (shaderFilename + ".msl");
		format = SDL_GPU_SHADERFORMAT_MSL;
		entrypoint = "main0";
	} else if (backendFormats & SDL_GPU_SHADERFORMAT_DXIL) {
		fullPath = BasePath / "Content/Shaders/Compiled/DXIL" / (shaderFilename + ".dxil");
		format = SDL_GPU_SHADERFORMAT_DXIL;
	} else {
		throw SDLException("No supported shader formats availavle");
	}

	std::ifstream file{fullPath,std::ios::binary};
	if(!file){throw std::runtime_error("Couldn't open shader file");}
	std::vector<Uint8> code{std::istreambuf_iterator{file},{}};

	SDL_GPUShaderCreateInfo shaderInfo{
		.code_size = code.size(),
		.code = code.data(),
		.entrypoint = entrypoint.c_str(),
		.format = format,
    	.stage = stage,
		.num_samplers = samplerCount,
		.num_storage_textures = storageTextureCount,
		.num_storage_buffers = storageBufferCount,
		.num_uniform_buffers = uniformBufferCount,
	};

	return SDL_CreateGPUShader(device, &shaderInfo);;
}

SDL_Surface* LoadImage(const std::string_view imageFilename, int desiredChannels)
{
	const auto fullPath{BasePath / "Content/Images" / imageFilename};
	SDL_PixelFormat format;

	std::cout << "Loading image: " << fullPath.string().c_str() << std::endl;
	auto result{IMG_Load(fullPath.string().c_str())};
	if (!result)
	{
		throw SDLException("Couldn't load image");
	}

	if (desiredChannels == 4)
	{
		format = SDL_PIXELFORMAT_ABGR8888;
	}
	else
	{
		SDL_DestroySurface(result);
		throw std::runtime_error("Unsupported number of channels requested");
	}
	if (result->format != format)
	{
		SDL_Surface *next = SDL_ConvertSurface(result, format);
		SDL_DestroySurface(result);
		result = next;
	}

	return result;
}

struct Vertex {
	glm::vec3 position;
	glm::vec2 uv;
};

int main(){
	try{
	
	BasePath = SDL_GetBasePath();

    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)){throw SDLException("SDL initialization failed");}
    auto *window{SDL_CreateWindow("Test Window",WIDTH,HEIGHT,SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE)};
    if(!window){throw SDLException("SDL_CreateWindow failed");}
    auto *device{SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true,nullptr)};
    if(!SDL_ClaimWindowForGPUDevice(device,window)){throw SDLException("Couldn't claim gpu device");}
    std::cout << ("{}",SDL_GetGPUDeviceDriver(device)) << std::endl;

	auto *vertexShader{LoadShader(device,"TexturedQuadWithMatrix.vert",0,1,0,0)};
    if(!vertexShader){throw SDLException("Couldn't load vertex shader");}

    auto *fragmentShader{LoadShader(device,"TexturedQuad.frag",1,0,0,0)};
    if(!fragmentShader){throw SDLException("Couldn't load fragment shader");}

	SDL_Surface *imageData{
		LoadImage("huhcat.png", 4)
	};
	std::array coloTargetDescriptions{
		SDL_GPUColorTargetDescription{
			.format = SDL_GetGPUSwapchainTextureFormat(device,window)
		}
	};
	SDL_GPUGraphicsPipelineTargetInfo targetInfo{
		.color_target_descriptions = coloTargetDescriptions.data(),
		.num_color_targets = coloTargetDescriptions.size(),
	};

	std::array<SDL_GPUVertexAttribute,2> vertexAttributes{
		{
			{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,static_cast<Uint32>(offsetof(Vertex,position))},
			{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,static_cast<Uint32>(offsetof(Vertex,uv))}
		}
	};

	std::array<SDL_GPUVertexBufferDescription,1> vertexBufferDescriptions{
		{
			{0,static_cast<Uint32>(sizeof(Vertex)),SDL_GPU_VERTEXINPUTRATE_VERTEX,0}
		}
	};

	SDL_GPUTextureFormat depthStencilFormat;

	if (SDL_GPUTextureSupportsFormat(
		device,
		SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
		SDL_GPU_TEXTURETYPE_2D,
		SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
	))
	{
		depthStencilFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
	}
	else if (SDL_GPUTextureSupportsFormat(
		device,
		SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
		SDL_GPU_TEXTURETYPE_2D,
		SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
	))
	{
		depthStencilFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
	}
	else
	{
		throw SDLException("No supported depth-stencil format found");
	}

	SDL_GPUGraphicsPipelineCreateInfo piplineCreateInfo{
		.vertex_shader = vertexShader,
		.fragment_shader = fragmentShader,
		.vertex_input_state = {
			.vertex_buffer_descriptions = vertexBufferDescriptions.data(),
			.num_vertex_buffers = static_cast<Uint32>(vertexBufferDescriptions.size()),
			.vertex_attributes = vertexAttributes.data(),
			.num_vertex_attributes = static_cast<Uint32>(vertexAttributes.size()),
		},
		.depth_stencil_state = {
			.compare_op = SDL_GPU_COMPAREOP_LESS,
			.enable_depth_test = true,
			.enable_depth_write = true,
			
		},
		.target_info = {
			.color_target_descriptions = coloTargetDescriptions.data(),
			.num_color_targets = coloTargetDescriptions.size(),
			.depth_stencil_format = depthStencilFormat,
			.has_depth_stencil_target = true,
		},
		
	};
	auto *pipline{SDL_CreateGPUGraphicsPipeline(device,&piplineCreateInfo)};
	if(!pipline){throw SDLException("Couldn't creat gpu graphics pipline");}

	SDL_ReleaseGPUShader(device,vertexShader);
	SDL_ReleaseGPUShader(device,fragmentShader);

	int windowWidth,windowHeight;
	SDL_GetWindowSize(window,&windowWidth,&windowHeight);
	SDL_GPUTextureCreateInfo imageTextureCreateInfo{
		.format = depthStencilFormat,
		.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
		.width = static_cast<Uint32>(windowWidth),
		.height = static_cast<Uint32>(windowHeight),
		.layer_count_or_depth = 1,
		.num_levels = 1,
	};
	auto depthStencilTargetTexture{SDL_CreateGPUTexture(device,&imageTextureCreateInfo)};
	if(!depthStencilTargetTexture){throw SDLException("Couldn't create depth stencil target texture");}

	SDL_GPUSamplerCreateInfo samplerCreateInfo{
		.min_filter = SDL_GPU_FILTER_LINEAR,
		.mag_filter = SDL_GPU_FILTER_LINEAR,
		.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
		.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
		.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
		.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
	};
	// LinearClamp
	auto sampler{SDL_CreateGPUSampler(device, &samplerCreateInfo)};
	if (!sampler) { throw SDLException("Couldn't create gpu sampler"); }

	SDL_GPUTextureCreateInfo textureCreateInfo{
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
		.width = static_cast<Uint32>(imageData->w),
		.height = static_cast<Uint32>(imageData->h),
		.layer_count_or_depth = 1,
		.num_levels = 1,
	};
	auto texture{SDL_CreateGPUTexture(device,&textureCreateInfo)};
	if(!texture){throw SDLException("Couldn't create gpu texture");}
	SDL_SetGPUTextureName(device,texture,"some texture");

	std::vector<Vertex> vertices{
    // 前面
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}},

    // 背面
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}},

    // 左面
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}},

    // 右面
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}},

    // 上面
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}},

    // 底面
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
	};
	std::vector<Uint32> indices{
    // 前面
    0, 1, 2, 2, 3, 0,
    // 背面
    4, 5, 6, 6, 7, 4,
    // 左面
    8, 9,10,10,11, 8,
    // 右面
   12,13,14,14,15,12,
    // 上面
   16,17,18,18,19,16,
    // 底面
   20,21,22,22,23,20,
	};


	SDL_GPUBufferCreateInfo vertexBufferCreateInfo{
		.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
		.size = static_cast<Uint32>(vertices.size() * sizeof(Vertex))
	};
	auto *vertexBuffer{SDL_CreateGPUBuffer(device,&vertexBufferCreateInfo)};
	if(!vertexBuffer){throw SDLException("Couldn't create gpu buffer");}
	SDL_SetGPUBufferName(device,vertexBuffer,"Vertex Buffer");

	SDL_GPUBufferCreateInfo indexBufferCreateInfo{
		.usage = SDL_GPU_BUFFERUSAGE_INDEX,
		.size = static_cast<Uint32>(indices.size() * sizeof(Uint32))
	};
	auto *indexBuffer{SDL_CreateGPUBuffer(device,&indexBufferCreateInfo)};
	if(!indexBuffer){throw SDLException("Couldn't create gpu buffer");}
	SDL_SetGPUBufferName(device,indexBuffer,"Index Buffer");

	SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = vertexBufferCreateInfo.size + indexBufferCreateInfo.size
	};
	auto transferBuffer{SDL_CreateGPUTransferBuffer(device,&transferBufferCreateInfo)};
	if(!transferBuffer){throw SDLException("Couldn't create transfer buffer");}
	
	auto transferBufferDataPtr{static_cast<Uint8 *>(SDL_MapGPUTransferBuffer(device,transferBuffer,false))};
	if(!transferBufferDataPtr){throw SDLException("Couldn't map transfer buffer");}

	std::span vertexBufferData{reinterpret_cast<Vertex *>(transferBufferDataPtr),vertices.size()};
	std::ranges::copy(vertices,vertexBufferData.begin());
	std::span indexBufferData{reinterpret_cast<Uint32 *>(transferBufferDataPtr + vertexBufferCreateInfo.size),indices.size()};
	std::ranges::copy(indices,indexBufferData.begin());

	SDL_UnmapGPUTransferBuffer(device,transferBuffer);

	SDL_GPUTransferBufferCreateInfo textureTransferBufferCreateInfo{
		.size = static_cast<Uint32>(imageData->pitch * imageData->h)
	};
	auto textureTransferBuffer{SDL_CreateGPUTransferBuffer(device,&textureTransferBufferCreateInfo)};
	if(!textureTransferBuffer){throw SDLException("Couldn't create texture transfer buffer");}
	
	auto textureTransferBufferDataPtr{static_cast<Uint8 *>(SDL_MapGPUTransferBuffer(device,textureTransferBuffer,false))};
	if(!textureTransferBufferDataPtr){throw SDLException("Couldn't map texture transfer buffer");}

	std::span textureDataSpan{textureTransferBufferDataPtr,textureTransferBufferCreateInfo.size};
	std::span imagePixels{
		static_cast<Uint8 *>(imageData->pixels),
		static_cast<size_t>(imageData->pitch * imageData->h)
	};

	std::ranges::copy(imagePixels,textureDataSpan.begin());
	SDL_UnmapGPUTransferBuffer(device,textureTransferBuffer);

	auto transferCommandBuffer{SDL_AcquireGPUCommandBuffer(device)};
	if(!transferCommandBuffer){throw SDLException("Couldn't acquire gpu command buffer");}
	auto copyPass{SDL_BeginGPUCopyPass(transferCommandBuffer)};
	SDL_GPUTransferBufferLocation source{
		.transfer_buffer = transferBuffer,
	};
	SDL_GPUBufferRegion destination{
		.buffer = vertexBuffer,
		.size = vertexBufferCreateInfo.size
	};
	SDL_UploadToGPUBuffer(copyPass,&source,&destination,false);

	source.offset = vertexBufferCreateInfo.size;
	destination.buffer = indexBuffer;
	destination.size = indexBufferCreateInfo.size;
	SDL_UploadToGPUBuffer(copyPass,&source,&destination,false);

	SDL_GPUTextureTransferInfo textureTransferInfo{
		.transfer_buffer = textureTransferBuffer
	};


	SDL_GPUTextureRegion textureRegion{
		.texture = texture,
		.w = static_cast<Uint32>(imageData->w),
		.h = static_cast<Uint32>(imageData->h),
		.d = 1
	};
	SDL_UploadToGPUTexture(copyPass,&textureTransferInfo,&textureRegion,false);

	
	SDL_EndGPUCopyPass(copyPass);
	if(!SDL_SubmitGPUCommandBuffer(transferCommandBuffer)){throw SDLException("Couldn't submit command buffer");}
	SDL_ReleaseGPUTransferBuffer(device,transferBuffer);
	SDL_ReleaseGPUTransferBuffer(device,textureTransferBuffer);
	SDL_DestroySurface(imageData);


    SDL_ShowWindow(window);
    auto isRunning{true};
    SDL_Event event;

	float windowAspectRatio{static_cast<float>(windowWidth) / static_cast<float>(windowHeight)};

    while(isRunning){
        while(SDL_PollEvent(&event)){
            switch (event.type){
                case SDL_EVENT_QUIT:
                    isRunning = false;
				case SDL_EVENT_WINDOW_RESIZED:{
					if(!SDL_GetWindowSize(window,&windowWidth,&windowHeight)){throw SDLException("Couldn't get window size");}
					windowAspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
					SDL_ReleaseGPUTexture(device,depthStencilTargetTexture);
					imageTextureCreateInfo.width = static_cast<Uint32>(windowWidth);
					imageTextureCreateInfo.height = static_cast<Uint32>(windowHeight);
					depthStencilTargetTexture = SDL_CreateGPUTexture(device,&imageTextureCreateInfo);
					if(!depthStencilTargetTexture){throw SDLException("Couldn't create depth stencil target texture");}
				}
				break;
                default: break;
            }
        }
        auto commandBuffer{SDL_AcquireGPUCommandBuffer(device)};
        if(!commandBuffer){throw SDLException("Couldn't aquire command buffer");}
        SDL_GPUTexture *swappinchainTexture;

        if(!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer,window,&swappinchainTexture,nullptr,nullptr))
		{throw SDLException("Couldn't acquire swapchain texture");}

        if(swappinchainTexture){
            std::array colorTargets{
				SDL_GPUColorTargetInfo{
					.texture = swappinchainTexture,
					.clear_color = SDL_FColor{0.1f,0.1f,0.1f,1.0f},
					.load_op = SDL_GPU_LOADOP_CLEAR
				}
			};
			SDL_GPUDepthStencilTargetInfo depthStencilTarget{
				.texture = depthStencilTargetTexture,
				.clear_depth = 1.0f,
				.load_op = SDL_GPU_LOADOP_CLEAR,
			};
            auto renderPass{SDL_BeginGPURenderPass(commandBuffer,colorTargets.data(),colorTargets.size(),&depthStencilTarget)};
			SDL_BindGPUGraphicsPipeline(renderPass,pipline);
			std::array<SDL_GPUBufferBinding,1> bindings{{vertexBuffer,0}};
			SDL_BindGPUVertexBuffers(renderPass,0,bindings.data(),bindings.size());
			SDL_GPUBufferBinding indexBufferBinding{indexBuffer,0};
			SDL_BindGPUIndexBuffer(renderPass,&indexBufferBinding,SDL_GPU_INDEXELEMENTSIZE_32BIT);
			
			std::array<SDL_GPUTextureSamplerBinding,1> texstureSamplerBindings{{texture,sampler}};
			SDL_BindGPUFragmentSamplers(renderPass,0,texstureSamplerBindings.data(),texstureSamplerBindings.size());

			
			auto projectionMatrix{glm::perspective(glm::radians(45.0f),windowAspectRatio,0.1f,100.0f)};
			auto viewMatrix{glm::lookAt(glm::vec3(0.0f,0.0f,2.0f),glm::vec3(0.0f,0.0f,0.0f),glm::vec3(0.0f,1.0f,0.0f))};
			auto modelMatrix{glm::mat4(1.0f)};
			
			auto imageAspectRatio{static_cast<float>(textureCreateInfo.width) / static_cast<float>(textureCreateInfo.height)};
			modelMatrix = glm::scale(modelMatrix,glm::vec3(imageAspectRatio * 0.5,0.5f,0.5f));
			auto ticks{SDL_GetTicks()};
			modelMatrix = glm::rotate(modelMatrix,glm::radians(static_cast<float>(ticks) * 0.1f),glm::vec3(1.0f,1.0f,1.0f));
			auto projectionViewMatrix{projectionMatrix * viewMatrix};
			auto modelViewProjectionMatrix{projectionViewMatrix * modelMatrix};
			SDL_PushGPUVertexUniformData(commandBuffer,0,&modelViewProjectionMatrix,sizeof(modelViewProjectionMatrix));

			SDL_DrawGPUIndexedPrimitives(renderPass,indices.size(),1,0,0,0);
            SDL_EndGPURenderPass(renderPass);
        }
        if(!SDL_SubmitGPUCommandBuffer(commandBuffer)){throw SDLException("Couldn't submit command buffer");}
        
    }
	

    SDL_Quit();
    return 0;
	}
    catch (const std::exception &e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }
}