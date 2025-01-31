/************************************************************************
* Minetest-c55
* Copyright (C) 2010-2011 celeron55, Perttu Ahola <celeron55@gmail.com>
*
* tile.cpp
* voxelands - 3d voxel world sandbox game
* Copyright (C) Lisa 'darkrose' Milne 2014 <lisa@ltmnet.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*
* License updated from GPLv2 or later to GPLv3 or later by Lisa Milne
* for Voxelands.
************************************************************************/

#include "common.h"

#include "tile.h"
#include "debug.h"
#include "main.h" // for g_settings
#include "game.h"
#include "utility.h"
#include "mesh.h"
#include "hex.h"
#include <ICameraSceneNode.h>
#include <IGUIStaticText.h>
#include "log.h"
#include "mapnode.h" // For texture atlas making
#include "mineral.h" // For texture atlas making
#include "path.h"
#include "base64.h"
#include "xCGUITTFont.h"

/*
	TextureSource
*/

TextureSource::TextureSource(IrrlichtDevice *device):
		m_device(device)
{
	assert(m_device);

	m_atlaspointer_cache_mutex.Init();

	m_main_thread = get_current_thread_id();

	// Add a NULL AtlasPointer as the first index, named ""
	m_atlaspointer_cache.push_back(SourceAtlasPointer(""));
	m_name_to_id[""] = 0;

	// Build main texture atlas
	if (config_get_bool("client.graphics.texture.atlas"))
		buildMainAtlas();
}

TextureSource::~TextureSource()
{
}

void TextureSource::processQueue()
{
	/*
		Fetch textures
	*/
	if (m_get_texture_queue.size() > 0) {
		GetRequest<std::string, u32, u8, u8>
				request = m_get_texture_queue.pop();

		infostream<<"TextureSource::processQueue(): "
				<<"got texture request with "
				<<"name=\""<<request.key<<"\""
				<<std::endl;

		GetResult<std::string, u32, u8, u8> result;
		result.key = request.key;
		result.callers = request.callers;
		result.item = getTextureIdDirect(request.key);
		/* TODO: segv right about here */
		request.dest->push_back(result);
	}
}

u32 TextureSource::getTextureId(const std::string &name)
{
	//infostream<<"getTextureId(): \""<<name<<"\""<<std::endl;

	{
		/*
			See if texture already exists
		*/
		JMutexAutoLock lock(m_atlaspointer_cache_mutex);
		core::map<std::string, u32>::Node *n;
		n = m_name_to_id.find(name);
		if (n != NULL)
			return n->getValue();
	}

	/*
		Get texture
	*/
	if (get_current_thread_id() == m_main_thread) {
		return getTextureIdDirect(name);
	}else{
		infostream<<"getTextureId(): Queued: name=\""<<name<<"\""<<std::endl;

		// We're gonna ask the result to be put into here
		ResultQueue<std::string, u32, u8, u8> result_queue;

		// Throw a request in
		m_get_texture_queue.add(name, 0, 0, &result_queue);

		infostream<<"Waiting for texture from main thread, name=\""
				<<name<<"\""<<std::endl;

		try{
			// Wait result for a second
			GetResult<std::string, u32, u8, u8>
					result = result_queue.pop_front(1000);

			// Check that at least something worked OK
			if (result.key != name)
				return 0;

			return result.item;
		}catch(ItemNotFoundException &e) {
			infostream<<"Waiting for texture timed out."<<std::endl;
			return 0;
		}
	}

	infostream<<"getTextureId(): Failed"<<std::endl;

	return 0;
}

// Draw a progress bar on the image
void make_progressbar(float value, video::IImage *image);

static void alpha_blit(IrrlichtDevice *device, video::IImage *dest, video::IImage *src, float d[4], float s[4], std::string name)
{
	std::string rtt_texture_name = name + "_RTT";
	video::ITexture *rtt = NULL;
	core::dimension2d<u32> rtt_dim = dest->getDimension();
	video::IVideoDriver *driver = device->getVideoDriver();
	if (driver->queryFeature(video::EVDF_RENDER_TO_TARGET))
		rtt = driver->addRenderTargetTexture(rtt_dim, rtt_texture_name.c_str(), video::ECF_A8R8G8B8);

	core::dimension2d<u32> src_dim = src->getDimension();

	core::rect<s32> dest_rect(d[0]*(float)rtt_dim.Width,d[1]*(float)rtt_dim.Height,d[2]*(float)rtt_dim.Width,d[3]*(float)rtt_dim.Height);
	core::rect<s32> src_rect(s[0]*(float)src_dim.Width,s[1]*(float)src_dim.Height,s[2]*(float)src_dim.Width,s[3]*(float)src_dim.Height);

	if (rtt == NULL) {
		if (src->getBitsPerPixel() == 32) {
			src->copyToWithAlpha(
				dest,
				dest_rect.UpperLeftCorner,
				src_rect,
				video::SColor(255,255,255,255),
				NULL
			);
		}else{
			src->copyTo(
				dest,
				dest_rect.UpperLeftCorner,
				src_rect,
				NULL
			);
		}
		return;
	}

	// Set render target
	driver->setRenderTarget(rtt, false, true, video::SColor(0,0,0,0));

	const video::SColor color(255,255,255,255);
	const video::SColor colors[] = {color,color,color,color};
	const core::rect<s32> rect(core::position2d<s32>(0,0),rtt_dim);
	const core::rect<s32> srect(core::position2d<s32>(0,0),rtt_dim);
	driver->beginScene(true, true, video::SColor(0,0,0,0));
	video::ITexture *t1 = driver->addTexture(std::string(rtt_texture_name+"_BASE").c_str(), dest);
	video::ITexture *t2 = driver->addTexture(std::string(rtt_texture_name+"_OVER").c_str(), src);
	driver->draw2DImage(
		t1,
		rect,
		srect,
		&rect,
		colors,
		true
	);
	driver->draw2DImage(
		t2,
		dest_rect,
		src_rect,
		&rect,
		colors,
		true
	);

	driver->endScene();

	// Unset render target
	driver->setRenderTarget(0, false, true, 0);

	// Create image of render target
	video::IImage *image = driver->createImage(rtt, v2s32(0,0), rtt_dim);
	if (image)
		image->copyTo(dest);
}

/*
	Draw an image on top of an another one, using the alpha channel of the
	source image; only modify fully opaque pixels in destinaion
*/
static void blit_with_alpha_overlay(video::IImage *src, video::IImage *dst,
		v2s32 src_pos, v2s32 dst_pos, v2u32 size)
{
	for (u32 y0=0; y0<size.Y; y0++) {
		for (u32 x0=0; x0<size.X; x0++) {
			s32 src_x = src_pos.X + x0;
			s32 src_y = src_pos.Y + y0;
			s32 dst_x = dst_pos.X + x0;
			s32 dst_y = dst_pos.Y + y0;
			video::SColor src_c = src->getPixel(src_x, src_y);
			video::SColor dst_c = dst->getPixel(dst_x, dst_y);
			if (dst_c.getAlpha() == 255 && src_c.getAlpha() != 0) {
				dst_c = src_c.getInterpolated(dst_c, (float)src_c.getAlpha()/255.0f);
				dst->setPixel(dst_x, dst_y, dst_c);
			}
		}
	}
}

/*
	Generate image based on a string like "stone.png" or "[crack0".
	if baseimg is NULL, it is created. Otherwise stuff is made on it.
*/
bool generate_image(std::string part_of_name, video::IImage *& baseimg,
		IrrlichtDevice *device);

/*
	Generates an image from a full string like
	"stone.png^mineral_coal.png^[crack0".

	This is used by buildMainAtlas().
*/
video::IImage* generate_image_from_scratch(std::string name,
		IrrlichtDevice *device);

/*
	This method generates all the textures
*/
u32 TextureSource::getTextureIdDirect(const std::string &name)
{
	//infostream<<"getTextureIdDirect(): name=\""<<name<<"\""<<std::endl;

	// Empty name means texture 0
	if (name == "") {
		infostream<<"getTextureIdDirect(): name is empty"<<std::endl;
		return 0;
	}

	/*
		Calling only allowed from main thread
	*/
	if (get_current_thread_id() != m_main_thread) {
		errorstream<<"TextureSource::getTextureIdDirect() called not from main thread"<<std::endl;
		return 0;
	}

	/*
		See if texture already exists
	*/
	{
		JMutexAutoLock lock(m_atlaspointer_cache_mutex);

		core::map<std::string, u32>::Node *n;
		n = m_name_to_id.find(name);
		if (n != NULL) {
			infostream<<"getTextureIdDirect(): \""<<name<<"\" found in cache"<<std::endl;
			return n->getValue();
		}
	}

	infostream<<"getTextureIdDirect(): \""<<name
			<<"\" NOT found in cache. Creating it."<<std::endl;

	/*
		Get the base image
	*/

	char separator = '^';

	/*
		This is set to the id of the base image.
		If left 0, there is no base image and a completely new image
		is made.
	*/
	u32 base_image_id = 0;

	// Find last meta separator in name
	s32 last_separator_position = -1;
	for (s32 i=name.size()-1; i>=0; i--) {
		if (name[i] == separator) {
			last_separator_position = i;
			break;
		}
	}
	/*
		If separator was found, construct the base name and make the
		base image using a recursive call
	*/
	std::string base_image_name;
	if (last_separator_position != -1) {
		// Construct base name
		base_image_name = name.substr(0, last_separator_position);
		/*infostream<<"getTextureIdDirect(): Calling itself recursively"
				" to get base image of \""<<name<<"\" = \""
				<<base_image_name<<"\""<<std::endl;*/
		base_image_id = getTextureIdDirect(base_image_name);
	}

	//infostream<<"base_image_id="<<base_image_id<<std::endl;

	video::IVideoDriver* driver = m_device->getVideoDriver();
	assert(driver);

	video::ITexture *t = NULL;

	/*
		An image will be built from files and then converted into a texture.
	*/
	video::IImage *baseimg = NULL;

	// If a base image was found, copy it to baseimg
	if (base_image_id != 0) {
		JMutexAutoLock lock(m_atlaspointer_cache_mutex);

		SourceAtlasPointer ap = m_atlaspointer_cache[base_image_id];

		video::IImage *image = ap.atlas_img;

		if (image == NULL) {
			infostream<<"getTextureIdDirect(): NULL image in "
					<<"cache: \""<<base_image_name<<"\""
					<<std::endl;
		}else{
			core::dimension2d<u32> dim = ap.intsize;

			baseimg = driver->createImage(video::ECF_A8R8G8B8, dim);

			core::position2d<s32> pos_to(0,0);
			core::position2d<s32> pos_from = ap.intpos;

			image->copyTo(
				baseimg, // target
				v2s32(0,0), // position in target
				core::rect<s32>(pos_from, dim) // from
			);

			/*infostream<<"getTextureIdDirect(): Loaded \""
					<<base_image_name<<"\" from image cache"
					<<std::endl;*/
		}
	}

	/*
		Parse out the last part of the name of the image and act
		according to it
	*/

	std::string last_part_of_name = name.substr(last_separator_position+1);
	//infostream<<"last_part_of_name=\""<<last_part_of_name<<"\""<<std::endl;

	// Generate image according to part of name
	if (generate_image(last_part_of_name, baseimg, m_device) == false) {
		infostream<<"getTextureIdDirect(): "
				"failed to generate \""<<last_part_of_name<<"\""
				<<std::endl;
	}

	// If no resulting image, print a warning
	if (baseimg == NULL) {
		infostream<<"getTextureIdDirect(): baseimg is NULL (attempted to"
				" create texture \""<<name<<"\""<<std::endl;
	}

	if (baseimg != NULL) {
		// Create texture from resulting image
		t = driver->addTexture(name.c_str(), baseimg);
	}

	/*
		Add texture to caches (add NULL textures too)
	*/

	JMutexAutoLock lock(m_atlaspointer_cache_mutex);

	u32 id = m_atlaspointer_cache.size();
	AtlasPointer ap(id);
	ap.atlas = t;
	ap.pos = v2f(0,0);
	ap.size = v2f(1,1);
	ap.tiled = 0;
	core::dimension2d<u32> baseimg_dim(0,0);
	if (baseimg)
		baseimg_dim = baseimg->getDimension();
	SourceAtlasPointer nap(name, ap, baseimg, v2s32(0,0), baseimg_dim);
	m_atlaspointer_cache.push_back(nap);
	m_name_to_id.insert(name, id);

	/*infostream<<"getTextureIdDirect(): "
			<<"Returning id="<<id<<" for name \""<<name<<"\""<<std::endl;*/

	return id;
}

std::string TextureSource::getTextureName(u32 id)
{
	JMutexAutoLock lock(m_atlaspointer_cache_mutex);

	if(id >= m_atlaspointer_cache.size())
	{
		infostream<<"TextureSource::getTextureName(): id="<<id
				<<" >= m_atlaspointer_cache.size()="
				<<m_atlaspointer_cache.size()<<std::endl;
		return "";
	}

	return m_atlaspointer_cache[id].name;
}


AtlasPointer TextureSource::getTexture(u32 id)
{
	JMutexAutoLock lock(m_atlaspointer_cache_mutex);

	if(id >= m_atlaspointer_cache.size())
		return AtlasPointer(0, NULL);

	return m_atlaspointer_cache[id].a;
}

void TextureSource::buildMainAtlas()
{
	infostream<<"TextureSource::buildMainAtlas()"<<std::endl;

	//return; // Disable (for testing)

	video::IVideoDriver* driver = m_device->getVideoDriver();
	assert(driver);

	JMutexAutoLock lock(m_atlaspointer_cache_mutex);

	// Create an image of the right size
	core::dimension2d<u32> atlas_dim(4096,4096);
	core::dimension2d<u32> max_dim = driver->getMaxTextureSize();
	atlas_dim.Width  = MYMIN(atlas_dim.Width,  max_dim.Width);
	atlas_dim.Height = MYMIN(atlas_dim.Height, max_dim.Height);
	video::IImage *atlas_img =
			driver->createImage(video::ECF_A8R8G8B8, atlas_dim);
	//assert(atlas_img);
	if(atlas_img == NULL)
	{
		errorstream<<"TextureSource::buildMainAtlas(): Failed to create atlas "
				"image; not building texture atlas."<<std::endl;
		return;
	}

	/*
		Grab list of stuff to include in the texture atlas from the
		main content features
	*/

	core::map<std::string, bool> sourcelist;

	for(u16 j=0; j<MAX_CONTENT+1; j++)
	{
		if(j == CONTENT_IGNORE || j == CONTENT_AIR)
			continue;
		ContentFeatures *f = &content_features(j);
		for(core::map<std::string, bool>::Iterator
				i = f->used_texturenames.getIterator();
				i.atEnd() == false; i++)
		{
			std::string name = i.getNode()->getKey();
			sourcelist[name] = true;

			if (f->often_contains_mineral) {
				for (int k=1; k<MINERAL_MAX; k++){
					std::string mineraltexture = mineral_features(k).texture;
					if (mineraltexture == "")
						continue;
					std::string fulltexture = name + "^" + mineraltexture;
					sourcelist[fulltexture] = true;
				}
			}
		}
	}

	infostream<<"Creating texture atlas out of textures: ";
	for(core::map<std::string, bool>::Iterator
			i = sourcelist.getIterator();
			i.atEnd() == false; i++)
	{
		std::string name = i.getNode()->getKey();
		infostream<<"\""<<name<<"\" ";
	}
	infostream<<std::endl;

	// Padding to disallow texture bleeding
	s32 padding = 16;

	s32 column_width = 256;
	s32 column_padding = 16;

	/*
		First pass: generate almost everything
	*/
	core::position2d<s32> pos_in_atlas(0,0);

	pos_in_atlas.X = column_padding;
	pos_in_atlas.Y = padding;

	for(core::map<std::string, bool>::Iterator
			i = sourcelist.getIterator();
			i.atEnd() == false; i++)
	{
		std::string name = i.getNode()->getKey();

		/*video::IImage *img = driver->createImageFromFile(
				getTexturePath(name.c_str()).c_str());
		if(img == NULL)
			continue;

		core::dimension2d<u32> dim = img->getDimension();
		// Make a copy with the right color format
		video::IImage *img2 =
				driver->createImage(video::ECF_A8R8G8B8, dim);
		img->copyTo(img2);
		img->drop();*/

		// Generate image by name
		video::IImage *img2 = generate_image_from_scratch(name, m_device);
		if(img2 == NULL)
		{
			infostream<<"TextureSource::buildMainAtlas(): Couldn't generate texture atlas: Couldn't generate image \""<<name<<"\""<<std::endl;
			continue;
		}

		core::dimension2d<u32> dim = img2->getDimension();

		// Don't add to atlas if image is large
		core::dimension2d<u32> max_size_in_atlas(64,64);
		if(dim.Width > max_size_in_atlas.Width
		|| dim.Height > max_size_in_atlas.Height)
		{
			infostream<<"TextureSource::buildMainAtlas(): Not adding "
					<<"\""<<name<<"\" because image is large"<<std::endl;
			continue;
		}

		// Wrap columns and stop making atlas if atlas is full
		if(pos_in_atlas.Y + dim.Height > atlas_dim.Height)
		{
			if(pos_in_atlas.X > (s32)atlas_dim.Width - column_width - column_padding){
				errorstream<<"TextureSource::buildMainAtlas(): "
						<<"Atlas is full, not adding more textures."
						<<std::endl;
				break;
			}
			pos_in_atlas.Y = padding;
			pos_in_atlas.X += column_width + column_padding*2;
		}

		infostream<<"TextureSource::buildMainAtlas(): Adding \""<<name
				<<"\" to texture atlas"<<std::endl;

		// Tile it a few times in the X direction
		u16 xwise_tiling = column_width / dim.Width;
		if(xwise_tiling > 16) // Limit to 16 (more gives no benefit)
			xwise_tiling = 16;
		for(u32 j=0; j<xwise_tiling; j++)
		{
			// Copy the copy to the atlas
			//img2->copyToWithAlpha(atlas_img,
					//pos_in_atlas + v2s32(j*dim.Width,0),
					//core::rect<s32>(v2s32(0,0), dim),
					//video::SColor(255,255,255,255),
					//NULL);
			img2->copyTo(atlas_img,
					pos_in_atlas + v2s32(j*dim.Width,0),
					core::rect<s32>(v2s32(0,0), dim),
					NULL);
		}


		// Copy the borders a few times to disallow texture bleeding
		for(u32 side=0; side<2; side++) // top and bottom
		for(s32 y0=0; y0<padding; y0++)
		for(s32 x0=0; x0<(s32)xwise_tiling*(s32)dim.Width; x0++)
		{
			s32 dst_y;
			s32 src_y;
			if(side==0)
			{
				dst_y = y0 + pos_in_atlas.Y + dim.Height;
				src_y = pos_in_atlas.Y + dim.Height - 1;
			}
			else
			{
				dst_y = -y0 + pos_in_atlas.Y-1;
				src_y = pos_in_atlas.Y;
			}
			s32 x = x0 + pos_in_atlas.X;
			video::SColor c = atlas_img->getPixel(x, src_y);
			atlas_img->setPixel(x,dst_y,c);
		}

		for(u32 side=0; side<2; side++) // left and right
		for(s32 x0=0; x0<column_padding; x0++)
		for(s32 y0=-padding; y0<(s32)dim.Height+padding; y0++)
		{
			s32 dst_x;
			s32 src_x;
			if(side==0)
			{
				dst_x = x0 + pos_in_atlas.X + dim.Width*xwise_tiling;
				src_x = pos_in_atlas.X + dim.Width*xwise_tiling - 1;
			}
			else
			{
				dst_x = -x0 + pos_in_atlas.X-1;
				src_x = pos_in_atlas.X;
			}
			s32 y = y0 + pos_in_atlas.Y;
			s32 src_y = MYMAX((int)pos_in_atlas.Y, MYMIN((int)pos_in_atlas.Y + (int)dim.Height - 1, y));
			s32 dst_y = y;
			video::SColor c = atlas_img->getPixel(src_x, src_y);
			atlas_img->setPixel(dst_x,dst_y,c);
		}

		img2->drop();

		/*
			Add texture to caches
		*/

		// Get next id
		u32 id = m_atlaspointer_cache.size();

		// Create AtlasPointer
		AtlasPointer ap(id);
		ap.atlas = NULL; // Set on the second pass
		ap.pos = v2f((float)pos_in_atlas.X/(float)atlas_dim.Width,
				(float)pos_in_atlas.Y/(float)atlas_dim.Height);
		ap.size = v2f((float)dim.Width/(float)atlas_dim.Width,
				(float)dim.Width/(float)atlas_dim.Height);
		ap.tiled = xwise_tiling;

		// Create SourceAtlasPointer and add to containers
		SourceAtlasPointer nap(name, ap, atlas_img, pos_in_atlas, dim);
		m_atlaspointer_cache.push_back(nap);
		m_name_to_id.insert(name, id);

		// Increment position
		pos_in_atlas.Y += dim.Height + padding * 2;
	}

	/*
		Make texture
	*/
	video::ITexture *t = driver->addTexture("__main_atlas__", atlas_img);
	assert(t);

	/*
		Second pass: set texture pointer in generated AtlasPointers
	*/
	for(core::map<std::string, bool>::Iterator
			i = sourcelist.getIterator();
			i.atEnd() == false; i++)
	{
		std::string name = i.getNode()->getKey();
		if(m_name_to_id.find(name) == NULL)
			continue;
		u32 id = m_name_to_id[name];
		//infostream<<"id of name "<<name<<" is "<<id<<std::endl;
		m_atlaspointer_cache[id].a.atlas = t;
	}

	/*
		Write image to file so that it can be inspected
	*/
	/*std::string atlaspath = porting::path_userdata
			+ DIR_DELIM + "generated_texture_atlas.png";
	infostream<<"Removing and writing texture atlas for inspection to "
			<<atlaspath<<std::endl;
	fs::RecursiveDelete(atlaspath);
	driver->writeImageToFile(atlas_img, atlaspath.c_str());*/
}

static bool parseHexColorString(const std::string &value, video::SColor &color)
{
	unsigned char components[] = { 0x00, 0x00, 0x00, 0xff }; // R,G,B,A

	if (value[0] != '#')
		return false;

	size_t len = value.size();
	bool short_form;

	if (len == 9 || len == 7) // #RRGGBBAA or #RRGGBB
		short_form = false;
	else if (len == 5 || len == 4) // #RGBA or #RGB
		short_form = true;
	else
		return false;

	bool success = true;

	for (size_t pos = 1, cc = 0; pos < len; pos++, cc++) {
		assert(cc < sizeof components / sizeof components[0]);
		if (short_form) {
			unsigned char d;
			if (!hex_digit_decode(value[pos], d)) {
				success = false;
				break;
			}
			components[cc] = (d & 0xf) << 4 | (d & 0xf);
		} else {
			unsigned char d1, d2;
			if (!hex_digit_decode(value[pos], d1) ||
					!hex_digit_decode(value[pos+1], d2)) {
				success = false;
				break;
			}
			components[cc] = (d1 & 0xf) << 4 | (d2 & 0xf);
			pos++;	// skip the second digit -- it's already used
		}
	}

	if (success) {
		color.setRed(components[0]);
		color.setGreen(components[1]);
		color.setBlue(components[2]);
		color.setAlpha(components[3]);
	}

	return success;
}

struct ColorContainer {
	ColorContainer();
	std::map<const std::string, u32> colors;
};

ColorContainer::ColorContainer()
{
	colors["aliceblue"]              = 0xf0f8ff;
	colors["antiquewhite"]           = 0xfaebd7;
	colors["aqua"]                   = 0x00ffff;
	colors["aquamarine"]             = 0x7fffd4;
	colors["azure"]                  = 0xf0ffff;
	colors["beige"]                  = 0xf5f5dc;
	colors["bisque"]                 = 0xffe4c4;
	colors["black"]                  = 0x000000;
	colors["blanchedalmond"]         = 0xffebcd;
	colors["blue"]                   = 0x0000ff;
	colors["blueviolet"]             = 0x8a2be2;
	colors["brown"]                  = 0xa52a2a;
	colors["burlywood"]              = 0xdeb887;
	colors["cadetblue"]              = 0x5f9ea0;
	colors["chartreuse"]             = 0x7fff00;
	colors["chocolate"]              = 0xd2691e;
	colors["coral"]                  = 0xff7f50;
	colors["cornflowerblue"]         = 0x6495ed;
	colors["cornsilk"]               = 0xfff8dc;
	colors["crimson"]                = 0xdc143c;
	colors["cyan"]                   = 0x00ffff;
	colors["darkblue"]               = 0x00008b;
	colors["darkcyan"]               = 0x008b8b;
	colors["darkgoldenrod"]          = 0xb8860b;
	colors["darkgray"]               = 0xa9a9a9;
	colors["darkgreen"]              = 0x006400;
	colors["darkkhaki"]              = 0xbdb76b;
	colors["darkmagenta"]            = 0x8b008b;
	colors["darkolivegreen"]         = 0x556b2f;
	colors["darkorange"]             = 0xff8c00;
	colors["darkorchid"]             = 0x9932cc;
	colors["darkred"]                = 0x8b0000;
	colors["darksalmon"]             = 0xe9967a;
	colors["darkseagreen"]           = 0x8fbc8f;
	colors["darkslateblue"]          = 0x483d8b;
	colors["darkslategray"]          = 0x2f4f4f;
	colors["darkturquoise"]          = 0x00ced1;
	colors["darkviolet"]             = 0x9400d3;
	colors["deeppink"]               = 0xff1493;
	colors["deepskyblue"]            = 0x00bfff;
	colors["dimgray"]                = 0x696969;
	colors["dodgerblue"]             = 0x1e90ff;
	colors["firebrick"]              = 0xb22222;
	colors["floralwhite"]            = 0xfffaf0;
	colors["forestgreen"]            = 0x228b22;
	colors["fuchsia"]                = 0xff00ff;
	colors["gainsboro"]              = 0xdcdcdc;
	colors["ghostwhite"]             = 0xf8f8ff;
	colors["gold"]                   = 0xffd700;
	colors["goldenrod"]              = 0xdaa520;
	colors["gray"]                   = 0x808080;
	colors["green"]                  = 0x008000;
	colors["greenyellow"]            = 0xadff2f;
	colors["honeydew"]               = 0xf0fff0;
	colors["hotpink"]                = 0xff69b4;
	colors["indianred "]             = 0xcd5c5c;
	colors["indigo "]                = 0x4b0082;
	colors["ivory"]                  = 0xfffff0;
	colors["khaki"]                  = 0xf0e68c;
	colors["lavender"]               = 0xe6e6fa;
	colors["lavenderblush"]          = 0xfff0f5;
	colors["lawngreen"]              = 0x7cfc00;
	colors["lemonchiffon"]           = 0xfffacd;
	colors["lightblue"]              = 0xadd8e6;
	colors["lightcoral"]             = 0xf08080;
	colors["lightcyan"]              = 0xe0ffff;
	colors["lightgoldenrodyellow"]   = 0xfafad2;
	colors["lightgray"]              = 0xd3d3d3;
	colors["lightgreen"]             = 0x90ee90;
	colors["lightpink"]              = 0xffb6c1;
	colors["lightsalmon"]            = 0xffa07a;
	colors["lightseagreen"]          = 0x20b2aa;
	colors["lightskyblue"]           = 0x87cefa;
	colors["lightslategray"]         = 0x778899;
	colors["lightsteelblue"]         = 0xb0c4de;
	colors["lightyellow"]            = 0xffffe0;
	colors["lime"]                   = 0x00ff00;
	colors["limegreen"]              = 0x32cd32;
	colors["linen"]                  = 0xfaf0e6;
	colors["magenta"]                = 0xff00ff;
	colors["maroon"]                 = 0x800000;
	colors["mediumaquamarine"]       = 0x66cdaa;
	colors["mediumblue"]             = 0x0000cd;
	colors["mediumorchid"]           = 0xba55d3;
	colors["mediumpurple"]           = 0x9370db;
	colors["mediumseagreen"]         = 0x3cb371;
	colors["mediumslateblue"]        = 0x7b68ee;
	colors["mediumspringgreen"]      = 0x00fa9a;
	colors["mediumturquoise"]        = 0x48d1cc;
	colors["mediumvioletred"]        = 0xc71585;
	colors["midnightblue"]           = 0x191970;
	colors["mintcream"]              = 0xf5fffa;
	colors["mistyrose"]              = 0xffe4e1;
	colors["moccasin"]               = 0xffe4b5;
	colors["navajowhite"]            = 0xffdead;
	colors["navy"]                   = 0x000080;
	colors["oldlace"]                = 0xfdf5e6;
	colors["olive"]                  = 0x808000;
	colors["olivedrab"]              = 0x6b8e23;
	colors["orange"]                 = 0xffa500;
	colors["orangered"]              = 0xff4500;
	colors["orchid"]                 = 0xda70d6;
	colors["palegoldenrod"]          = 0xeee8aa;
	colors["palegreen"]              = 0x98fb98;
	colors["paleturquoise"]          = 0xafeeee;
	colors["palevioletred"]          = 0xdb7093;
	colors["papayawhip"]             = 0xffefd5;
	colors["peachpuff"]              = 0xffdab9;
	colors["peru"]                   = 0xcd853f;
	colors["pink"]                   = 0xffc0cb;
	colors["plum"]                   = 0xdda0dd;
	colors["powderblue"]             = 0xb0e0e6;
	colors["purple"]                 = 0x800080;
	colors["red"]                    = 0xff0000;
	colors["rosybrown"]              = 0xbc8f8f;
	colors["royalblue"]              = 0x4169e1;
	colors["saddlebrown"]            = 0x8b4513;
	colors["salmon"]                 = 0xfa8072;
	colors["sandybrown"]             = 0xf4a460;
	colors["seagreen"]               = 0x2e8b57;
	colors["seashell"]               = 0xfff5ee;
	colors["sienna"]                 = 0xa0522d;
	colors["silver"]                 = 0xc0c0c0;
	colors["skyblue"]                = 0x87ceeb;
	colors["slateblue"]              = 0x6a5acd;
	colors["slategray"]              = 0x708090;
	colors["snow"]                   = 0xfffafa;
	colors["springgreen"]            = 0x00ff7f;
	colors["steelblue"]              = 0x4682b4;
	colors["tan"]                    = 0xd2b48c;
	colors["teal"]                   = 0x008080;
	colors["thistle"]                = 0xd8bfd8;
	colors["tomato"]                 = 0xff6347;
	colors["turquoise"]              = 0x40e0d0;
	colors["violet"]                 = 0xee82ee;
	colors["wheat"]                  = 0xf5deb3;
	colors["white"]                  = 0xffffff;
	colors["whitesmoke"]             = 0xf5f5f5;
	colors["yellow"]                 = 0xffff00;
	colors["yellowgreen"]            = 0x9acd32;

}

static const ColorContainer named_colors;

static bool parseNamedColorString(const std::string &value, video::SColor &color)
{
	std::string color_name;
	std::string alpha_string;

	/* If the string has a # in it, assume this is the start of a specified
	 * alpha value (if it isn't the string is invalid and the error will be
	 * caught later on, either because the color name won't be found or the
	 * alpha value will fail conversion)
	 */
	size_t alpha_pos = value.find('#');
	if (alpha_pos != std::string::npos) {
		color_name = value.substr(0, alpha_pos);
		alpha_string = value.substr(alpha_pos + 1);
	}else{
		color_name = value;
	}

	color_name = lowercase(value);

	std::map<const std::string, unsigned>::const_iterator it;
	it = named_colors.colors.find(color_name);
	if (it == named_colors.colors.end())
		return false;

	u32 color_temp = it->second;

	/* An empty string for alpha is ok (none of the color table entries
	 * have an alpha value either). Color strings without an alpha specified
	 * are interpreted as fully opaque
	 *
	 * For named colors the supplied alpha string (representing a hex value)
	 * must be exactly two digits. For example:  colorname#08
	 */
	if (!alpha_string.empty()) {
		if (alpha_string.length() != 2)
			return false;

		unsigned char d1, d2;
		if (
			!hex_digit_decode(alpha_string.at(0), d1)
			|| !hex_digit_decode(alpha_string.at(1), d2)
		)
			return false;
		color_temp |= ((d1 & 0xf) << 4 | (d2 & 0xf)) << 24;
	}else{
		color_temp |= 0xff << 24;  // Fully opaque
	}

	color = video::SColor(color_temp);

	return true;
}

bool parseColorString(const std::string &value, video::SColor &color, bool quiet)
{
	bool success;

	if (value[0] == '#') {
		success = parseHexColorString(value, color);
	}else{
		success = parseNamedColorString(value, color);
	}

	if (!success && !quiet)
		errorstream << "Invalid color: \"" << value << "\"" << std::endl;

	return success;
}

video::IImage* generate_image_from_scratch(std::string name,
		IrrlichtDevice *device)
{
	/*infostream<<"generate_image_from_scratch(): "
			"\""<<name<<"\""<<std::endl;*/

	video::IVideoDriver* driver = device->getVideoDriver();
	assert(driver);

	/*
		Get the base image
	*/

	video::IImage *baseimg = NULL;

	char separator = '^';

	// Find last meta separator in name
	s32 last_separator_position = -1;
	for (s32 i=name.size()-1; i>=0; i--) {
		if (name[i] == separator) {
			last_separator_position = i;
			break;
		}
	}

	/*infostream<<"generate_image_from_scratch(): "
			<<"last_separator_position="<<last_separator_position
			<<std::endl;*/

	/*
		If separator was found, construct the base name and make the
		base image using a recursive call
	*/
	std::string base_image_name;
	if (last_separator_position != -1) {
		// Construct base name
		base_image_name = name.substr(0, last_separator_position);
		/*infostream<<"generate_image_from_scratch(): Calling itself recursively"
				" to get base image of \""<<name<<"\" = \""
				<<base_image_name<<"\""<<std::endl;*/
		baseimg = generate_image_from_scratch(base_image_name, device);
	}

	/*
		Parse out the last part of the name of the image and act
		according to it
	*/

	std::string last_part_of_name = name.substr(last_separator_position+1);

	//infostream<<"last_part_of_name=\""<<last_part_of_name<<"\""<<std::endl;

	// Generate image according to part of name
	if (generate_image(last_part_of_name, baseimg, device) == false) {
		infostream<<"generate_image_from_scratch(): "
				"failed to generate \""<<last_part_of_name<<"\""
				<<std::endl;
		return NULL;
	}

	return baseimg;
}

bool generate_image(std::string part_of_name, video::IImage *& baseimg,
		IrrlichtDevice *device)
{
	char buff[1024];
	video::IVideoDriver* driver = device->getVideoDriver();
	assert(driver);
	if (part_of_name == "")
		return baseimg;

	// Stuff starting with [ are special commands
	if (part_of_name[0] != '[') {
		// A normal texture; load it from a file

		video::IImage *image = NULL;
		if (path_get((char*)"texture",const_cast<char*>(part_of_name.c_str()),1,buff,1024))
			image = driver->createImageFromFile(buff);

		if (image == NULL) {
			if (part_of_name != "") {
				infostream<<"generate_image(): Could not load image \""
				<<part_of_name<<"\" while building texture"<<std::endl;

				infostream<<"generate_image(): Creating a dummy"
				<<" image for \""<<part_of_name<<"\""<<std::endl;
			}

			// Just create a dummy image
			core::dimension2d<u32> dim(1,1);
			image = driver->createImage(video::ECF_A8R8G8B8, dim);
			assert(image);
			image->setPixel(0,0, video::SColor(255,myrand()%256,
					myrand()%256,myrand()%256));
		}

		// If base image is NULL, load as base.
		if (baseimg == NULL) {
			//infostream<<"Setting "<<part_of_name<<" as base"<<std::endl;
			/*
				Copy it this way to get an alpha channel.
				Otherwise images with alpha cannot be blitted on
				images that don't have alpha in the original file.
			*/
			core::dimension2d<u32> dim = image->getDimension();
			baseimg = driver->createImage(video::ECF_A8R8G8B8, dim);
			image->copyTo(baseimg);
			image->drop();
		}else{
			float p[4] = {0.,0.,1.,1.};
			alpha_blit(device,baseimg,image,p,p,part_of_name);
			// Drop image
			image->drop();
		}
	}
	else
	{
		// A special texture modification

		infostream<<"generate_image(): generating special "
				<<"modification \""<<part_of_name<<"\""
				<<std::endl;

		/*
			This is the simplest of all; it just adds stuff to the
			name so that a separate texture is created.

			It is used to make textures for stuff that doesn't want
			to implement getting the texture from a bigger texture
			atlas.
		*/
		if(part_of_name == "[forcesingle")
		{
		}
		/*
			[crackN
			Adds a cracking texture
		*/
		else if (part_of_name.substr(0,6) == "[crack") {
			if (baseimg == NULL) {
				infostream<<"generate_image(): baseimg==NULL "
						<<"for part_of_name=\""<<part_of_name
						<<"\", cancelling."<<std::endl;
				return false;
			}

			// Crack image number
			u16 progression = mystoi(part_of_name.substr(6));

			/*
				Load crack image.

				It is an image with a number of cracking stages
				horizontally tiled.
			*/
			video::IImage *img_crack = NULL;
			if (path_get((char*)"texture",(char*)"crack.png",1,buff,1024))
				img_crack = driver->createImageFromFile(buff);

			if (!img_crack)
				return true;
			{
				// Dimension of original image
				core::dimension2d<u32> dim_crack = img_crack->getDimension();
				// Count of crack stages
				u32 crack_count = dim_crack.Height / dim_crack.Width;
				// Limit progression
				if(progression > crack_count-1)
					progression = crack_count-1;

				float s = 1.0/((float)crack_count);

				float dst[4] = {0.,0.,1.,1.};
				float src[4] = {0.,(s*progression),1.,(s*progression)+s};

				alpha_blit(device,baseimg,img_crack,dst,src,part_of_name);

				img_crack->drop();
			}
		}
		/*
			[combine:WxH:X,Y=filename:X,Y=filename2
			Creates a bigger texture from an amount of smaller ones
		*/
		else if(part_of_name.substr(0,8) == "[combine")
		{
			Strfnd sf(part_of_name);
			sf.next(":");
			u32 w0 = mystoi(sf.next("x"));
			u32 h0 = mystoi(sf.next(":"));
			infostream<<"combined w="<<w0<<" h="<<h0<<std::endl;
			core::dimension2d<u32> dim(w0,h0);
			baseimg = driver->createImage(video::ECF_A8R8G8B8, dim);
			while(sf.atend() == false)
			{
				u32 x = mystoi(sf.next(","));
				u32 y = mystoi(sf.next("="));
				std::string filename = sf.next(":");
				infostream<<"Adding \""<<filename
						<<"\" to combined ("<<x<<","<<y<<")"
						<<std::endl;

				if (path_get((char*)"texture",const_cast<char*>(filename.c_str()),1,buff,1024)) {
					video::IImage *img = driver->createImageFromFile(buff);
					if (img) {
						core::dimension2d<u32> dim = img->getDimension();
						infostream<<"Size "<<dim.Width
								<<"x"<<dim.Height<<std::endl;
						core::position2d<s32> pos_base(x, y);
						video::IImage *img2 =
								driver->createImage(video::ECF_A8R8G8B8, dim);
						img->copyTo(img2);
						img->drop();
						img2->copyToWithAlpha(baseimg, pos_base,
								core::rect<s32>(v2s32(0,0), dim),
								video::SColor(255,255,255,255),
								NULL);
						img2->drop();
					}else{
						infostream<<"img==NULL"<<std::endl;
					}
				}
			}
		}
		/*
			[progressbarN
			Adds a progress bar, 0.0 <= N <= 1.0
		*/
		else if(part_of_name.substr(0,12) == "[progressbar")
		{
			if(baseimg == NULL)
			{
				infostream<<"generate_image(): baseimg==NULL "
						<<"for part_of_name=\""<<part_of_name
						<<"\", cancelling."<<std::endl;
				return false;
			}

			float value = mystof(part_of_name.substr(12));
			make_progressbar(value, baseimg);
		}
		/*
			"[noalpha:filename.png"
			Use an image without it's alpha channel.
			Used for the leaves texture when in old leaves mode, so
			that the transparent parts don't look completely black
			when simple alpha channel is used for rendering.
		*/
		else if(part_of_name.substr(0,8) == "[noalpha")
		{
			if(baseimg != NULL)
			{
				infostream<<"generate_image(): baseimg!=NULL "
						<<"for part_of_name=\""<<part_of_name
						<<"\", cancelling."<<std::endl;
				return false;
			}

			std::string filename = part_of_name.substr(9);

			if (path_get((char*)"texture",const_cast<char*>(filename.c_str()),1,buff,1024)) {

				infostream<<"generate_image(): Loading path \""<<buff
						<<"\""<<std::endl;

				video::IImage *image = driver->createImageFromFile(buff);

				if (image == NULL) {
					infostream<<"generate_image(): Loading path \""
							<<buff<<"\" failed"<<std::endl;
				}else{
					core::dimension2d<u32> dim = image->getDimension();
					baseimg = driver->createImage(video::ECF_A8R8G8B8, dim);

					// Set alpha to full
					for(u32 y=0; y<dim.Height; y++)
					for(u32 x=0; x<dim.Width; x++)
					{
						video::SColor c = image->getPixel(x,y);
						c.setAlpha(255);
						image->setPixel(x,y,c);
					}
					// Blit
					image->copyTo(baseimg);

					image->drop();
				}
			}
		}
		/*
			"[makealpha:R,G,B:filename.png"
			Use an image with converting one color to transparent.
		*/
		else if(part_of_name.substr(0,11) == "[makealpha:")
		{
			if(baseimg != NULL)
			{
				infostream<<"generate_image(): baseimg!=NULL "
						<<"for part_of_name=\""<<part_of_name
						<<"\", cancelling."<<std::endl;
				return false;
			}

			Strfnd sf(part_of_name.substr(11));
			u32 r1 = mystoi(sf.next(","));
			u32 g1 = mystoi(sf.next(","));
			u32 b1 = mystoi(sf.next(":"));
			std::string filename = sf.next("");

			if (path_get((char*)"texture",const_cast<char*>(filename.c_str()),1,buff,1024)) {

				infostream<<"generate_image(): Loading path \""<<buff<<"\""<<std::endl;

				video::IImage *image = driver->createImageFromFile(buff);

				if (image == NULL) {
					infostream<<"generate_image(): Loading path \""
							<<buff<<"\" failed"<<std::endl;
				}else{
					core::dimension2d<u32> dim = image->getDimension();
					baseimg = driver->createImage(video::ECF_A8R8G8B8, dim);

					// Blit
					image->copyTo(baseimg);

					image->drop();

					for(u32 y=0; y<dim.Height; y++)
					for(u32 x=0; x<dim.Width; x++)
					{
						video::SColor c = baseimg->getPixel(x,y);
						u32 r = c.getRed();
						u32 g = c.getGreen();
						u32 b = c.getBlue();
						if(!(r == r1 && g == g1 && b == b1))
							continue;
						c.setAlpha(0);
						baseimg->setPixel(x,y,c);
					}
				}
			}
		}
		/*
			"[makealpha2:R,G,B;R2,G2,B2:filename.png"
			Use an image with converting two colors to transparent.
		*/
		else if(part_of_name.substr(0,12) == "[makealpha2:")
		{
			if(baseimg != NULL)
			{
				infostream<<"generate_image(): baseimg!=NULL "
						<<"for part_of_name=\""<<part_of_name
						<<"\", cancelling."<<std::endl;
				return false;
			}

			Strfnd sf(part_of_name.substr(12));
			u32 r1 = mystoi(sf.next(","));
			u32 g1 = mystoi(sf.next(","));
			u32 b1 = mystoi(sf.next(";"));
			u32 r2 = mystoi(sf.next(","));
			u32 g2 = mystoi(sf.next(","));
			u32 b2 = mystoi(sf.next(":"));
			std::string filename = sf.next("");

			if (path_get((char*)"texture",const_cast<char*>(filename.c_str()),1,buff,1024)) {

				infostream<<"generate_image(): Loading path \""<<buff<<"\""<<std::endl;

				video::IImage *image = driver->createImageFromFile(buff);

				if (image == NULL) {
					infostream<<"generate_image(): Loading path \""
							<<buff<<"\" failed"<<std::endl;
				}else{
					core::dimension2d<u32> dim = image->getDimension();
					baseimg = driver->createImage(video::ECF_A8R8G8B8, dim);

					// Blit
					image->copyTo(baseimg);

					image->drop();

					for(u32 y=0; y<dim.Height; y++)
					for(u32 x=0; x<dim.Width; x++)
					{
						video::SColor c = baseimg->getPixel(x,y);
						u32 r = c.getRed();
						u32 g = c.getGreen();
						u32 b = c.getBlue();
						if(!(r == r1 && g == g1 && b == b1) &&
						   !(r == r2 && g == g2 && b == b2))
							continue;
						c.setAlpha(0);
						baseimg->setPixel(x,y,c);
					}
				}
			}
		}
		/*
			"[transformN"
			Rotates and/or flips the image.

			N can be a number (between 0 and 7) or a transform name.
			Rotations are counter-clockwise.
			0  I      identity
			1  R90    rotate by 90 degrees
			2  R180   rotate by 180 degrees
			3  R270   rotate by 270 degrees
			4  FX     flip X
			5  FXR90  flip X then rotate by 90 degrees
			6  FY     flip Y
			7  FYR90  flip Y then rotate by 90 degrees

			Note: Transform names can be concatenated to produce
			their product (applies the first then the second).
			The resulting transform will be equivalent to one of the
			eight existing ones, though (see: dihedral group).
		*/
		else if(part_of_name.substr(0,10) == "[transform")
		{
			if(baseimg == NULL)
			{
				errorstream<<"generate_image(): baseimg==NULL "
						<<"for part_of_name=\""<<part_of_name
						<<"\", cancelling."<<std::endl;
				return false;
			}

			u32 transform = parseImageTransform(part_of_name.substr(10));
			core::dimension2d<u32> dim = imageTransformDimension(
					transform, baseimg->getDimension());
			video::IImage *image = driver->createImage(
					baseimg->getColorFormat(), dim);
			assert(image);
			imageTransform(transform, baseimg, image);
			baseimg->drop();
			baseimg = image;
		}
		/*
			[inventorycube{topimage{leftimage{rightimage
			In every subimage, replace ^ with &.
			Create an "inventory cube".
			NOTE: This should be used only on its own.
			Example (a grass block (not actually used in game):
			"[inventorycube{grass.png{mud.png&grass_side.png{mud.png&grass_side.png"
		*/
		else if(part_of_name.substr(0,14) == "[inventorycube")
		{
			if(baseimg != NULL)
			{
				errorstream<<"generate_image(): baseimg!=NULL "
						<<"for part_of_name=\""<<part_of_name
						<<"\", cancelling."<<std::endl;
				return false;
			}

			str_replace_char(part_of_name, '&', '^');
			Strfnd sf(part_of_name);
			sf.next("{");
			std::string imagename_top = sf.next("{");
			std::string imagename_left = sf.next("{");
			std::string imagename_right = sf.next("{");

			// Generate images for the faces of the cube
			video::IImage *img_top = generate_image_from_scratch(
					imagename_top, device);
			video::IImage *img_left = generate_image_from_scratch(
					imagename_left, device);
			video::IImage *img_right = generate_image_from_scratch(
					imagename_right, device);
			assert(img_top && img_left && img_right);

			// Create textures from images
			video::ITexture *texture_top = driver->addTexture(
					(imagename_top + "__temp__").c_str(), img_top);
			video::ITexture *texture_left = driver->addTexture(
					(imagename_left + "__temp__").c_str(), img_left);
			video::ITexture *texture_right = driver->addTexture(
					(imagename_right + "__temp__").c_str(), img_right);
			assert(texture_top && texture_left && texture_right);

			// Drop images
			img_top->drop();
			img_left->drop();
			img_right->drop();

			/*
				Draw a cube mesh into a render target texture
			*/
			scene::IMesh* cube = createCubeMesh(v3f(1, 1, 1));
			setMeshColor(cube, video::SColor(255, 255, 255, 255));
			cube->getMeshBuffer(0)->getMaterial().setTexture(0, texture_top);
			cube->getMeshBuffer(1)->getMaterial().setTexture(0, texture_top);
			cube->getMeshBuffer(2)->getMaterial().setTexture(0, texture_right);
			cube->getMeshBuffer(3)->getMaterial().setTexture(0, texture_right);
			cube->getMeshBuffer(4)->getMaterial().setTexture(0, texture_left);
			cube->getMeshBuffer(5)->getMaterial().setTexture(0, texture_left);

			core::dimension2d<u32> dim(64,64);
			std::string rtt_texture_name = part_of_name + "_RTT";

			v3f camera_position(0, 1.0, -1.5);
			camera_position.rotateXZBy(45);
			v3f camera_lookat(0, 0, 0);
			core::CMatrix4<f32> camera_projection_matrix;
			// Set orthogonal projection
			camera_projection_matrix.buildProjectionMatrixOrthoLH(
					1.65, 1.65, 0, 100);

			video::SColorf ambient_light(0.2,0.2,0.2);
			v3f light_position(10, 100, -50);
			video::SColorf light_color(0.5,0.5,0.5);
			f32 light_radius = 1000;

			video::ITexture *rtt = generateTextureFromMesh(
					cube, device, dim, rtt_texture_name,
					camera_position,
					camera_lookat,
					camera_projection_matrix,
					ambient_light,
					light_position,
					light_color,
					light_radius);

			// Drop mesh
			cube->drop();

			// Free textures of images
			driver->removeTexture(texture_top);
			driver->removeTexture(texture_left);
			driver->removeTexture(texture_right);

			if(rtt == NULL)
			{
				baseimg = generate_image_from_scratch(
						imagename_top, device);
				return true;
			}

			// Create image of render target
			video::IImage *image = driver->createImage(rtt, v2s32(0,0), dim);
			assert(image);

			baseimg = driver->createImage(video::ECF_A8R8G8B8, dim);

			if(image)
			{
				image->copyTo(baseimg);
				image->drop();
			}
		}
		/*
			[inventorynode{nodeid{topimage{leftimage{rightimage
			In every subimage, replace ^ with &.
			Create an "inventory stair".
			NOTE: This should be used only on its own.
			Example (a grass stair (not actually used in game):
			"[inventorynode{2048{grass.png{mud.png&grass_side.png{mud.png&grass_side.png"
			TODO: not implemented, only creates a cube
		*/
		else if(part_of_name.substr(0,14) == "[inventorynode")
		{
			if(baseimg != NULL)
			{
				errorstream<<"generate_image(): baseimg!=NULL "
						<<"for part_of_name=\""<<part_of_name
						<<"\", cancelling."<<std::endl;
				return false;
			}

			str_replace_char(part_of_name, '&', '^');
			Strfnd sf(part_of_name);
			sf.next("{");
			std::string node_id = sf.next("{");
			std::string imagename_top = sf.next("{");
			std::string imagename_left = sf.next("{");
			std::string imagename_right = sf.next("{");

			content_t c = atoi(node_id.c_str());

			// Generate images for the faces of the cube
			video::IImage *img_top = generate_image_from_scratch(
					imagename_top, device);
			video::IImage *img_left = generate_image_from_scratch(
					imagename_left, device);
			video::IImage *img_right = generate_image_from_scratch(
					imagename_right, device);
			assert(img_top && img_left && img_right);

			// Create textures from images
			video::ITexture *texture_top = driver->addTexture(
					(imagename_top + "__temp__").c_str(), img_top);
			video::ITexture *texture_left = driver->addTexture(
					(imagename_left + "__temp__").c_str(), img_left);
			video::ITexture *texture_right = driver->addTexture(
					(imagename_right + "__temp__").c_str(), img_right);
			assert(texture_top && texture_left && texture_right);

			// Drop images
			img_top->drop();
			img_left->drop();
			img_right->drop();

			/*
				Draw a mesh into a render target texture
			*/
			std::vector<NodeBox> boxes = content_features(c).getWieldNodeBoxes();
			scene::IMesh* cube = createNodeBoxMesh(boxes,v3f(1, 1, 1));
			setMeshColor(cube, video::SColor(255, 255, 255, 255));
			for (u16 i=0; i < boxes.size(); i++) {
				cube->getMeshBuffer((i*6)+0)->getMaterial().setTexture(0, texture_top);
				cube->getMeshBuffer((i*6)+1)->getMaterial().setTexture(0, texture_top);
				cube->getMeshBuffer((i*6)+2)->getMaterial().setTexture(0, texture_right);
				cube->getMeshBuffer((i*6)+3)->getMaterial().setTexture(0, texture_right);
				cube->getMeshBuffer((i*6)+4)->getMaterial().setTexture(0, texture_left);
				cube->getMeshBuffer((i*6)+5)->getMaterial().setTexture(0, texture_left);
			}

			core::dimension2d<u32> dim(64,64);
			std::string rtt_texture_name = part_of_name + "_RTT";

			v3f camera_position(0, 1.0, -1.5);
			camera_position.rotateXZBy(45);
			v3f camera_lookat(0, 0, 0);
			core::CMatrix4<f32> camera_projection_matrix;
			// Set orthogonal projection
			camera_projection_matrix.buildProjectionMatrixOrthoLH(
					1.65, 1.65, 0, 100);

			video::SColorf ambient_light(0.2,0.2,0.2);
			v3f light_position(10, 100, -50);
			video::SColorf light_color(0.5,0.5,0.5);
			f32 light_radius = 1000;

			video::ITexture *rtt = generateTextureFromMesh(
					cube, device, dim, rtt_texture_name,
					camera_position,
					camera_lookat,
					camera_projection_matrix,
					ambient_light,
					light_position,
					light_color,
					light_radius);

			// Drop mesh
			cube->drop();

			// Free textures of images
			driver->removeTexture(texture_top);
			driver->removeTexture(texture_left);
			driver->removeTexture(texture_right);

			if(rtt == NULL)
			{
				baseimg = generate_image_from_scratch(
						imagename_top, device);
				return true;
			}

			// Create image of render target
			video::IImage *image = driver->createImage(rtt, v2s32(0,0), dim);
			assert(image);

			baseimg = driver->createImage(video::ECF_A8R8G8B8, dim);

			if(image)
			{
				image->copyTo(baseimg);
				image->drop();
			}
		}
		/*
			[colorize:color
			Overlays image with given color
			color = color as ColorString
		*/
		else if (part_of_name.substr(0,10) == "[colorize:") {
			Strfnd sf(part_of_name);
			sf.next(":");
			std::string color_str = sf.next(":");

			if (baseimg == NULL) {
				errorstream << "generateImagePart(): baseimg != NULL "
						<< "for part_of_name=\"" << part_of_name
						<< "\", cancelling." << std::endl;
				return false;
			}

			video::SColor color;
			if (!parseColorString(color_str, color, false))
				return false;

			core::dimension2d<u32> dim = baseimg->getDimension();
			video::IImage *img = driver->createImage(video::ECF_A8R8G8B8, dim);

			if (!img) {
				errorstream << "generateImagePart(): Could not create image "
						<< "for part_of_name=\"" << part_of_name
						<< "\", cancelling." << std::endl;
				return false;
			}

			img->fill(video::SColor(color));
			// Overlay the colored image
			blit_with_alpha_overlay(img, baseimg, v2s32(0,0), v2s32(0,0), dim);
			img->drop();
		}
		/*
			[verticalframe:N:I
			Crops a frame of a vertical animation.
			N = frame count, I = frame index
		*/
		else if(part_of_name.substr(0,15) == "[verticalframe:")
		{
			Strfnd sf(part_of_name);
			sf.next(":");
			u32 frame_count = mystoi(sf.next(":"));
			u32 frame_index = mystoi(sf.next(":"));

			if(baseimg == NULL){
				errorstream<<"generate_image(): baseimg!=NULL "
						<<"for part_of_name=\""<<part_of_name
						<<"\", cancelling."<<std::endl;
				return false;
			}

			v2u32 frame_size = baseimg->getDimension();
			frame_size.Y /= frame_count;

			video::IImage *img = driver->createImage(video::ECF_A8R8G8B8,
					frame_size);
			if (!img) {
				errorstream <<"generate_image(): Could not create image "
						<< "for part_of_name=\"" << part_of_name
						<< "\", cancelling." << std::endl;
				return false;
			}

			// Fill target image with transparency
			img->fill(video::SColor(0,0,0,0));

			core::dimension2d<u32> dim = frame_size;
			core::position2d<s32> pos_dst(0, 0);
			core::position2d<s32> pos_src(0, frame_index * frame_size.Y);
			baseimg->copyToWithAlpha(img, pos_dst,
					core::rect<s32>(pos_src, dim),
					video::SColor(255,255,255,255),
					NULL);
			// Replace baseimg
			baseimg->drop();
			baseimg = img;
		}
		/*
			[text:x,y,X,Y,string
			writes string to texture
		*/
		else if (part_of_name.substr(0,6) == "[text:") {
			Strfnd sf(part_of_name);
			sf.next(":");
			std::string x = sf.next(",");
			std::string y = sf.next(",");
			std::string X = sf.next(",");
			std::string Y = sf.next(",");
			std::wstring text = narrow_to_wide(base64_decode(sf.end()));

			if (baseimg == NULL) {
				errorstream << "generateImagePart(): baseimg == NULL "
						<< "for part_of_name=\"" << part_of_name
						<< "\", cancelling." << std::endl;
				return false;
			}

			core::rect<f32> pos(
				mystof(x.c_str()),
				mystof(y.c_str()),
				mystof(X.c_str()),
				mystof(Y.c_str())
			);

			video::IVideoDriver *driver = device->getVideoDriver();
			if (driver->queryFeature(video::EVDF_RENDER_TO_TARGET) == false) {
				static bool warned = false;
				if (!warned) {
					errorstream<<"generateImagePart(): EVDF_RENDER_TO_TARGET not supported."<<std::endl;
					warned = true;
				}
				return false;
			}

			core::dimension2d<u32> dim = baseimg->getDimension();
			core::dimension2d<u32> rtt_dim(dim.Width*10,dim.Height*10);
			std::string rtt_texture_name = part_of_name + "_RTT";

			// Create render target texture
			video::ITexture *rtt = driver->addRenderTargetTexture(rtt_dim, rtt_texture_name.c_str(), video::ECF_A8R8G8B8);
			if (rtt == NULL) {
				errorstream<<"generateImagePart(): addRenderTargetTexture"
						" returned NULL."<<std::endl;
				return false;
			}

			// Get the gui
			gui::IGUIEnvironment *guienv = device->getGUIEnvironment();
			assert(guienv);

			gui::IGUISkin* skin = guienv->getSkin();
			gui::IGUIFont *std_font = skin->getFont();
			static gui::IGUIFont *tex_font = NULL;
#if USE_FREETYPE
			if (path_get("font","unifont.ttf",1,buff,1024)) {
				int sz = 10*((dim.Width/16)+1);
				if (sz < 10)
					sz = 12;
				tex_font = gui::CGUITTFont::createTTFont(guienv, buff,sz);
			}
#else
			if (path_get((char*)"texture",(char*)"fontlucida.png",1,buff,1024))
				tex_font = guienv->getFont(buff);
#endif
			if (tex_font)
				skin->setFont(tex_font);

			// Set render target
			driver->setRenderTarget(rtt, false, true, video::SColor(0,0,0,0));

			const video::SColor color(255,255,255,255);
			const video::SColor colors[] = {color,color,color,color};
			const core::rect<s32> rect(core::position2d<s32>(0,0),rtt_dim);
			const core::rect<s32> srect(core::position2d<s32>(0,0),dim);
			driver->beginScene(true, true, video::SColor(255,0,0,0));
			video::ITexture *t = driver->addTexture(std::string(rtt_texture_name+"_BASE").c_str(), baseimg);
			driver->draw2DImage(
				t,
				rect,
				srect,
				&rect,
				colors,
				true
			);

			const core::rect<s32> trect(
				(f32)rtt_dim.Width*pos.UpperLeftCorner.X,
				(f32)rtt_dim.Height*pos.UpperLeftCorner.Y,
				(f32)rtt_dim.Width*pos.LowerRightCorner.X,
				(f32)rtt_dim.Height*pos.LowerRightCorner.Y
			);
			gui::IGUIStaticText *e = guienv->addStaticText(text.c_str(), trect);
			e->setTextAlignment(gui::EGUIA_CENTER,gui::EGUIA_CENTER);

			// Render scene
			e->draw();
			//guienv->drawAll();
			driver->endScene();

			// remove that text so it doesn't appear in the game window for
			// some insane irrlicht too many classes reason
			e->remove();

			// Unset render target
			driver->setRenderTarget(0, false, true, 0);

			skin->setFont(std_font);

			// Create image of render target
			video::IImage *image = driver->createImage(rtt, v2s32(0,0), rtt_dim);
			assert(image);

			video::IImage *new_baseimg = driver->createImage(video::ECF_A8R8G8B8, rtt_dim);
			if (new_baseimg) {
				baseimg->copyToScaling(new_baseimg);
				baseimg->drop();
				baseimg = new_baseimg;
			}

			if (image) {
				image->copyTo(baseimg);
				image->drop();
			}
		}
		/*
			[blit:x,y,X,Y,string
			blits (part of) an image over the current image
		*/
		else if (part_of_name.substr(0,6) == "[blit:") {
			//infostream<<"Blitting "<<part_of_name<<" on base"<<std::endl;
			// Size of the copied area
			Strfnd sf(part_of_name);
			sf.next(":");
			float x = mystof(sf.next(","));
			float y = mystof(sf.next(","));
			float X = mystof(sf.next(","));
			float Y = mystof(sf.next(","));
			std::string path = sf.end();
			if (path_get((char*)"texture",const_cast<char*>(path.c_str()),1,buff,1024)) {
				video::IImage *image = driver->createImageFromFile(buff);

				if (baseimg == NULL) {
					errorstream << "generateImagePart(): baseimg == NULL "
							<< "for part_of_name=\"" << part_of_name
							<< "\", cancelling." << std::endl;
					return false;
				}
				if (image == NULL) {
					errorstream << "generateImagePart(): image == NULL "
							<< "for part_of_name=\"" << part_of_name
							<< "\", cancelling." << std::endl;
					return false;
				}
				float p[4] = {x,y,X,Y};
				alpha_blit(device,baseimg,image,p,p,part_of_name);
				// Drop image
				image->drop();
			}
		}else{
			infostream<<"generate_image(): Invalid "
					" modification: \""<<part_of_name<<"\""<<std::endl;
		}
	}

	return true;
}

void make_progressbar(float value, video::IImage *image)
{
	if(image == NULL)
		return;

	core::dimension2d<u32> size = image->getDimension();

	u32 barheight = size.Height/16;
	u32 barpad_x = size.Width/16;
	u32 barpad_y = size.Height/16;
	u32 barwidth = size.Width - barpad_x*2;
	v2u32 barpos(barpad_x, size.Height - barheight - barpad_y);

	u32 barvalue_i = (u32)(((float)barwidth * value) + 0.5);
	u32 barvalue_c[10] = {
		(u32)(((float)barwidth * 0.1)),
		(u32)(((float)barwidth * 0.2)),
		(u32)(((float)barwidth * 0.3)),
		(u32)(((float)barwidth * 0.4)),
		(u32)(((float)barwidth * 0.5)),
		(u32)(((float)barwidth * 0.6)),
		(u32)(((float)barwidth * 0.7)),
		(u32)(((float)barwidth * 0.8)),
		(u32)(((float)barwidth * 0.9)),
		(u32)(((float)barwidth * 1.0))
	};

	video::SColor active[10] = {
		video::SColor(255,255,0,0),
		video::SColor(255,255,40,0),
		video::SColor(255,255,80,0),
		video::SColor(255,255,110,0),
		video::SColor(255,255,120,0),
		video::SColor(255,255,140,0),
		video::SColor(255,255,160,0),
		video::SColor(255,170,180,0),
		video::SColor(255,50,200,0),
		video::SColor(255,0,255,0)
	};
	video::SColor inactive(255,0,0,0);
	for(u32 x0=0; x0<barwidth; x0++)
	{
		video::SColor *c;
		if (x0 < barvalue_i) {
			if (x0 < barvalue_c[0]) {
				c = &active[0];
			}else if (x0 < barvalue_c[1]) {
				c = &active[1];
			}else if (x0 < barvalue_c[2]) {
				c = &active[2];
			}else if (x0 < barvalue_c[3]) {
				c = &active[3];
			}else if (x0 < barvalue_c[4]) {
				c = &active[4];
			}else if (x0 < barvalue_c[5]) {
				c = &active[5];
			}else if (x0 < barvalue_c[6]) {
				c = &active[6];
			}else if (x0 < barvalue_c[7]) {
				c = &active[7];
			}else if (x0 < barvalue_c[8]) {
				c = &active[8];
			}else{
				c = &active[9];
			}
		}else{
			c = &inactive;
		}
		u32 x = x0 + barpos.X;
		for(u32 y=barpos.Y; y<barpos.Y+barheight; y++)
		{
			image->setPixel(x,y, *c);
		}
	}
}

u32 parseImageTransform(const std::string& s)
{
	int total_transform = 0;

	std::string transform_names[8];
	transform_names[0] = "i";
	transform_names[1] = "r90";
	transform_names[2] = "r180";
	transform_names[3] = "r270";
	transform_names[4] = "fx";
	transform_names[6] = "fy";

	std::size_t pos = 0;
	while(pos < s.size())
	{
		int transform = -1;
		for(int i = 0; i <= 7; ++i)
		{
			const std::string &name_i = transform_names[i];

			if(s[pos] == ('0' + i))
			{
				transform = i;
				pos++;
				break;
			}
			else if(!(name_i.empty()) &&
				lowercase(s.substr(pos, name_i.size())) == name_i)
			{
				transform = i;
				pos += name_i.size();
				break;
			}
		}
		if(transform < 0)
			break;

		// Multiply total_transform and transform in the group D4
		int new_total = 0;
		if(transform < 4)
			new_total = (transform + total_transform) % 4;
		else
			new_total = (transform - total_transform + 8) % 4;
		if((transform >= 4) ^ (total_transform >= 4))
			new_total += 4;

		total_transform = new_total;
	}
	return total_transform;
}

core::dimension2d<u32> imageTransformDimension(u32 transform, core::dimension2d<u32> dim)
{
	if(transform % 2 == 0)
		return dim;
	else
		return core::dimension2d<u32>(dim.Height, dim.Width);
}

void imageTransform(u32 transform, video::IImage *src, video::IImage *dst)
{
	if(src == NULL || dst == NULL)
		return;

	core::dimension2d<u32> srcdim = src->getDimension();
	core::dimension2d<u32> dstdim = dst->getDimension();

	assert(dstdim == imageTransformDimension(transform, srcdim));
	assert(transform >= 0 && transform <= 7);

	/*
		Compute the transformation from source coordinates (sx,sy)
		to destination coordinates (dx,dy).
	*/
	int sxn = 0;
	int syn = 2;
	if(transform == 0)         // identity
		sxn = 0, syn = 2;  //   sx = dx, sy = dy
	else if(transform == 1)    // rotate by 90 degrees ccw
		sxn = 3, syn = 0;  //   sx = (H-1) - dy, sy = dx
	else if(transform == 2)    // rotate by 180 degrees
		sxn = 1, syn = 3;  //   sx = (W-1) - dx, sy = (H-1) - dy
	else if(transform == 3)    // rotate by 270 degrees ccw
		sxn = 2, syn = 1;  //   sx = dy, sy = (W-1) - dx
	else if(transform == 4)    // flip x
		sxn = 1, syn = 2;  //   sx = (W-1) - dx, sy = dy
	else if(transform == 5)    // flip x then rotate by 90 degrees ccw
		sxn = 2, syn = 0;  //   sx = dy, sy = dx
	else if(transform == 6)    // flip y
		sxn = 0, syn = 3;  //   sx = dx, sy = (H-1) - dy
	else if(transform == 7)    // flip y then rotate by 90 degrees ccw
		sxn = 3, syn = 1;  //   sx = (H-1) - dy, sy = (W-1) - dx

	for(u32 dy=0; dy<dstdim.Height; dy++)
	for(u32 dx=0; dx<dstdim.Width; dx++)
	{
		u32 entries[4] = {dx, dstdim.Width-1-dx, dy, dstdim.Height-1-dy};
		u32 sx = entries[sxn];
		u32 sy = entries[syn];
		video::SColor c = src->getPixel(sx,sy);
		dst->setPixel(dx,dy,c);
	}
}
