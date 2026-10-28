#pragma once

// Textures shipped in Data/SKSE/Plugins/CheatMenu: menu icons (map markers exported from the game's map.swf) and
// key glyphs (the game's own Interface/exported/*.png.dds). Loaded on first use, cached, released with the device.
namespace Textures
{
	struct Image
	{
		unsigned long long id{ 0 };  // ImTextureID; 0 when the file is missing or broken
		float              width{ 0 };
		float              height{ 0 };

		explicit operator bool() const { return id != 0; }
	};

	void Init(void* a_device);  // ID3D11Device*

	// a_name is relative to the folder and has no extension: "icons/home", "keys/esc".
	// A failed load is remembered, so a missing file is looked for only once.
	const Image& Get(std::string_view a_name);

	void ReleaseAll();
}
