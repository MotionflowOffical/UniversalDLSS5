# UniversalDLSS5 GameGuides adapter template

A game-specific adapter is optional. Build a DLL named exactly `UniversalDLSS5.GameGuides.dll` and place it beside the target game executable. UniversalDLSS5 explicitly loads that absolute path after injection; it is not a DXGI/proxy DLL.

Export `UdlssGameGuides_GetFrameV1` using the contract in `include/udlss/game_guides_api.hpp`. Returned D3D11 textures must be AddRef'd. Prefer same-resolution, non-MSAA resources. Motion vectors should be R16G16_FLOAT current-to-previous pixel reprojection. Control masks should be RGBA8 with R=1 apply NR and R=0 bypass NR. Normals/albedo can be published for diagnostics, but v0.2.6 does not bind undocumented Feature-18 parameter names for them.
