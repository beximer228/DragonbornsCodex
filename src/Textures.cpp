#include "Textures.h"

#include <d3d11.h>

namespace Textures
{
	namespace
	{
		const std::filesystem::path kRoot{ "Data/SKSE/Plugins/DragonbornsCodex" };

		ID3D11Device*                                 g_device{ nullptr };
		std::unordered_map<std::string, Image>        g_cache;
		std::mutex                                    g_mutex;

		constexpr std::uint32_t FourCC(char a, char b, char c, char d)
		{
			return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b) << 8 | static_cast<std::uint32_t>(c) << 16 | static_cast<std::uint32_t>(d) << 24;
		}

#pragma pack(push, 1)
		struct PixelFormat
		{
			std::uint32_t size, flags, fourCC, rgbBits, rMask, gMask, bMask, aMask;
		};
		struct Header
		{
			std::uint32_t magic, size, flags, height, width, pitch, depth, mipCount;
			std::uint32_t reserved[11];
			PixelFormat   format;
			std::uint32_t caps[4], reserved2;
		};
		struct HeaderDX10
		{
			std::uint32_t format, dimension, miscFlags, arraySize, miscFlags2;
		};
#pragma pack(pop)
		static_assert(sizeof(Header) == 128);

		// Block-compressed or 32-bit DDS, which is all the game and our export use. Returns the format and where the
		// pixels start, or DXGI_FORMAT_UNKNOWN.
		DXGI_FORMAT Parse(const std::vector<char>& a_file, const Header*& a_header, std::size_t& a_offset)
		{
			if (a_file.size() < sizeof(Header)) {
				return DXGI_FORMAT_UNKNOWN;
			}
			a_header = reinterpret_cast<const Header*>(a_file.data());
			a_offset = sizeof(Header);
			if (a_header->magic != FourCC('D', 'D', 'S', ' ') || a_header->size != 124) {
				return DXGI_FORMAT_UNKNOWN;
			}
			const auto& pf = a_header->format;
			if (pf.flags & 0x4) {  // DDPF_FOURCC
				switch (pf.fourCC) {
				case FourCC('D', 'X', 'T', '1'):
					return DXGI_FORMAT_BC1_UNORM;
				case FourCC('D', 'X', 'T', '2'):
				case FourCC('D', 'X', 'T', '3'):
					return DXGI_FORMAT_BC2_UNORM;
				case FourCC('D', 'X', 'T', '4'):
				case FourCC('D', 'X', 'T', '5'):
					return DXGI_FORMAT_BC3_UNORM;
				case FourCC('D', 'X', '1', '0'):
					if (a_file.size() < sizeof(Header) + sizeof(HeaderDX10)) {
						return DXGI_FORMAT_UNKNOWN;
					}
					a_offset += sizeof(HeaderDX10);
					return static_cast<DXGI_FORMAT>(reinterpret_cast<const HeaderDX10*>(a_file.data() + sizeof(Header))->format);
				default:
					return DXGI_FORMAT_UNKNOWN;
				}
			}
			if ((pf.flags & 0x40) && pf.rgbBits == 32) {  // DDPF_RGB
				if (pf.rMask == 0x00FF0000 && pf.bMask == 0x000000FF) {
					return DXGI_FORMAT_B8G8R8A8_UNORM;
				}
				if (pf.rMask == 0x000000FF && pf.bMask == 0x00FF0000) {
					return DXGI_FORMAT_R8G8B8A8_UNORM;
				}
			}
			return DXGI_FORMAT_UNKNOWN;
		}

		std::size_t BlockBytes(DXGI_FORMAT a_format)
		{
			switch (a_format) {
			case DXGI_FORMAT_BC1_UNORM:
			case DXGI_FORMAT_BC4_UNORM:
				return 8;
			case DXGI_FORMAT_BC2_UNORM:
			case DXGI_FORMAT_BC3_UNORM:
			case DXGI_FORMAT_BC5_UNORM:
			case DXGI_FORMAT_BC7_UNORM:
				return 16;
			default:
				return 0;  // not block-compressed
			}
		}

		Image Load(const std::filesystem::path& a_path)
		{
			std::ifstream in{ a_path, std::ios::binary };
			if (!in) {
				return {};
			}
			std::vector<char> file{ std::istreambuf_iterator<char>{ in }, {} };
			const Header*     header = nullptr;
			std::size_t       offset = 0;
			const auto        format = Parse(file, header, offset);
			if (format == DXGI_FORMAT_UNKNOWN) {
				logger::warn("Textures: unsupported DDS {}", a_path.string());
				return {};
			}

			const UINT width = header->width, height = header->height;
			const UINT mips = std::max<UINT>(1, (header->flags & 0x20000) ? header->mipCount : 1);
			const auto block = BlockBytes(format);
			std::vector<D3D11_SUBRESOURCE_DATA> levels;
			std::size_t                         at = offset;
			for (UINT i = 0, w = width, h = height; i < mips; ++i, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
				const std::size_t pitch = block ? std::max<std::size_t>(1, (w + 3) / 4) * block : static_cast<std::size_t>(w) * 4;
				const std::size_t rows = block ? std::max<std::size_t>(1, (h + 3) / 4) : h;
				if (at + pitch * rows > file.size()) {
					break;
				}
				levels.push_back({ file.data() + at, static_cast<UINT>(pitch), 0 });
				at += pitch * rows;
			}
			if (levels.empty()) {
				return {};
			}

			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = static_cast<UINT>(levels.size());
			desc.ArraySize = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			ID3D11Texture2D* texture = nullptr;
			if (FAILED(g_device->CreateTexture2D(&desc, levels.data(), &texture))) {
				logger::warn("Textures: CreateTexture2D failed for {}", a_path.string());
				return {};
			}
			ID3D11ShaderResourceView* view = nullptr;
			const auto                hr = g_device->CreateShaderResourceView(texture, nullptr, &view);
			texture->Release();
			if (FAILED(hr)) {
				return {};
			}
			return { reinterpret_cast<unsigned long long>(view), static_cast<float>(width), static_cast<float>(height) };
		}
	}

	void Init(void* a_device) { g_device = static_cast<ID3D11Device*>(a_device); }

	const Image& Get(std::string_view a_name)
	{
		std::lock_guard lock{ g_mutex };
		const std::string key{ a_name };
		if (const auto it = g_cache.find(key); it != g_cache.end()) {
			return it->second;
		}
		Image image;
		if (g_device) {
			const auto path = kRoot / (key + ".dds");
			image = Load(path);
			if (!image) {
				logger::info("Textures: {} not available, using the drawn fallback", path.string());
			}
		}
		return g_cache.emplace(key, image).first->second;
	}

	void ReleaseAll()
	{
		std::lock_guard lock{ g_mutex };
		for (auto& [name, image] : g_cache) {
			if (image.id) {
				reinterpret_cast<ID3D11ShaderResourceView*>(image.id)->Release();
			}
		}
		g_cache.clear();
	}
}
