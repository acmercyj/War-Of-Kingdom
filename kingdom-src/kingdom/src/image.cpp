/* $Id: image.cpp 47608 2010-11-21 01:56:29Z shadowmaster $ */
/*
   Copyright (C) 2003 - 2010 by David White <dave@whitevine.net>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

/**
 * @file
 * Routines for images: load, scale, re-color, etc.
 */

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "global.hpp"

#include "color_range.hpp"
#include "config.hpp"
#include "filesystem.hpp"
#include "foreach.hpp"
#include "game_config.hpp"
#include "image.hpp"
#include "image_function.hpp"
#include "log.hpp"
#include "gettext.hpp"
#include "serialization/string_utils.hpp"

#include "SDL_image.h"

#include <boost/functional/hash.hpp>

#include <list>
#include <set>

#include "posix.h"

static lg::log_domain log_display("display");
#define ERR_DP LOG_STREAM(err, log_display)
#define LOG_DP LOG_STREAM(info, log_display)


std::list<int> dummy_list;

#if (defined(__APPLE__) && TARGET_OS_IPHONE) || defined(ANDROID)
const size_t locator_table_size = 6000;
#else
const size_t locator_table_size = 30000;
#endif

struct locator_node {
	size_t hash;
	size_t hash1;
	int index;
};

template<typename T>
struct cache_item
{
	cache_item(): 
		item(), 
		pos_in_locator_table(-1),
		position(dummy_list.end())
	{}

	T item;
	int pos_in_locator_table;
	std::list<int>::iterator position;
};

namespace image {

template<typename T>
class cache_type
{
public:
	cache_type(bool clear_cookie = true) :
			cache_max_size_(locator_table_size / 3),
			lru_list_(),
			content_(),
			clear_cookie_(clear_cookie)
	{
		// content_.resize(cache_max_size_);
		content_ = new cache_item<T>[cache_max_size_];

		for (int index = cache_max_size_ - 1; index >= 0; index --) {
			lru_list_.push_front(index);
			content_[index].position = lru_list_.begin();
		}
		for (int index = 0; index < locator_table_size; index ++) {
			locator_table_[index].index = -1;
		}
	}
	~cache_type() 
	{
		delete []content_;
	}

	void flush() 
	{ 
		if (clear_cookie_) {
			for (int index = cache_max_size_ - 1; index >= 0; index --) {
				cache_item<T>& elt = content_[index];
				if (elt.pos_in_locator_table != -1) {
					locator_table_[elt.pos_in_locator_table].index = -1;
				}
				elt.pos_in_locator_table = -1;
			}
		}
	}
	int add(const T& item, size_t hash, size_t hash1);

	bool verify_pos();
public:
	
	int cache_max_size_;
	bool clear_cookie_;
    std::list<int> lru_list_;
	// std::vector<cache_item<T> > content_;
	cache_item<T>* content_;
	locator_node locator_table_[locator_table_size];
};

template<typename T>
bool cache_type<T>::verify_pos()
{
	size_t valid_in_locator_table, valid_in_content;

	valid_in_locator_table = 0;
	for (int index = 0; index < locator_table_size; index ++) {
		if (locator_table_[index].index != -1) {
			valid_in_locator_table ++;
		}
	}

	valid_in_content = 0;
	for (int index = cache_max_size_ - 1; index >= 0; index --) {
		cache_item<T>& elt = content_[index];
		if (elt.pos_in_locator_table != -1) {
			valid_in_content ++;
		}
	}
	return valid_in_locator_table == valid_in_content;
}

template<typename T>
int cache_type<T>::add(const T &item, size_t hash, size_t hash1)
{
	// in locator_table, find a empty position.
	size_t pos = hash % locator_table_size;
	while (locator_table_[pos].index != -1) {
		if (++ pos == locator_table_size) {
			pos = 0;
		}
	}

	// calcuate index of content_
	int index = lru_list_.back();

	cache_item<T>& elt = content_[index];
	if (elt.pos_in_locator_table != -1) {
		locator_table_[elt.pos_in_locator_table].index = -1;
	}
	elt.item = item;
	elt.pos_in_locator_table = pos;

	lru_list_.erase(elt.position);
	lru_list_.push_front(index);
	elt.position = lru_list_.begin();

	// fill (hash,hash1,index)
	locator_table_[pos].hash = hash;
	locator_table_[pos].hash1 = hash1;
	locator_table_[pos].index = index;

	return index;
}

template <typename T>
int locator::in_cache(cache_type<T> &cache) const
{
	int stop_after_invalids = 4;

	locator_node* locator_table = cache.locator_table_;

	size_t pos = hash_ % locator_table_size;
	do {
		while (locator_table[pos].index != -1) {
			locator_node* node = locator_table + pos;
			if (node->hash == hash_ && node->hash1 == hash1_) {
				return node->index;
			}
			if (++ pos == locator_table_size) {
				pos = 0;
			}
		}
		if (++ pos == locator_table_size) {
			pos = 0;
		}
	} while (-- stop_after_invalids);
	return -1;
}

template <typename T>
const T &locator::locate_in_cache(cache_type<T> &cache, int index) const
{
	static T dummy;
	if (index < 0) {
		return dummy;
	}
	std::list<int>& lru_list = cache.lru_list_;
	cache_item<T>& elt = cache.content_[index];
	if (index != lru_list.front()) {
		lru_list.erase(elt.position);
		lru_list.push_front(index);
		elt.position = lru_list.begin();
	}
	return elt.item;
}

template <typename T>
void locator::add_to_cache(cache_type<T> &cache, const T &data) const
{
	cache.add(data, hash_, hash1_);
}

}

namespace image {

int tile_size = 72;

}

namespace {

/** Definition of all image maps */
image::image_cache images_(false);
image::image_cache scaled_to_zoom_,
		scaled_to_hex_images_,
		tod_colored_images_,
		brightened_images_;
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
image::image_cache semi_brightened_images_;
#endif

// cache storing if each image fit in a hex
image::bool_cache in_hex_info_;

// cache storing if this is an empty hex
image::bool_cache is_empty_hex_;

// const int cache_version_ = 0;

std::map<std::string,bool> image_existence_map;

// directories where we already cached file existence
std::set<std::string> precached_dirs;

std::map<surface, surface> reversed_images_;

int red_adjust = 0, green_adjust = 0, blue_adjust = 0;

/** List of colors used by the TC image modification */
std::vector<std::string> team_colors;

int zoom = image::tile_size;

int cached_zoom = 0;

} // end anon namespace

void image_verify_pos()
{
	images_.verify_pos();
}

namespace image {

mini_terrain_cache_map mini_terrain_cache;
mini_terrain_cache_map mini_fogged_terrain_cache;

void flush_cache()
{
	images_.flush();
	tod_colored_images_.flush();
	scaled_to_zoom_.flush();
	scaled_to_hex_images_.flush();
	brightened_images_.flush();
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
	semi_brightened_images_.flush();
#endif
	in_hex_info_.flush();
	is_empty_hex_.flush();
	mini_terrain_cache.clear();
	mini_fogged_terrain_cache.clear();
	reversed_images_.clear();
	image_existence_map.clear();
	precached_dirs.clear();
}

bool locator::operator==(const locator &a) const 
{
	return (hash_ == a.hash_ && hash1_ == a.hash1_); 
}

void locator::parse_arguments()
{
	std::string& fn = val_.filename_;
	if(fn.empty()) {
		return;
	}
	size_t markup_field = fn.find('~');

	if(markup_field != std::string::npos) {
		val_.type_ = SUB_FILE;
		val_.modifications_ = fn.substr(markup_field, fn.size() - markup_field);
		fn = fn.substr(0,markup_field);
	}
}

locator::locator() :
	val_()
{
	hash_ = hash_value(val_);
	hash1_ = hash_value1(val_);
}

locator::locator(const locator &a, const std::string& mods):
	val_(a.val_)
{
	if (mods.size()){
		val_.modifications_ += mods;
		val_.type_=SUB_FILE;
	}
	hash_ = hash_value(val_);
	hash1_ = hash_value1(val_);
}

locator::locator(const char *filename) :
	val_(filename)
{
	parse_arguments();
	hash_ = hash_value(val_);
	hash1_ = hash_value1(val_);
}

locator::locator(const std::string &filename) :
	val_(filename)
{
	parse_arguments();
	hash_ = hash_value(val_);
	hash1_ = hash_value1(val_);
}

locator::locator(const std::string &filename, const std::string& modifications) :
	val_(filename, modifications)
{
	hash_ = hash_value(val_);
	hash1_ = hash_value1(val_);
}

locator::locator(const std::string &filename, const map_location &loc,
		int center_x, int center_y, const std::string& modifications) :
	val_(filename, loc, center_x, center_y, modifications)
{
	hash_ = hash_value(val_);
	hash1_ = hash_value1(val_);
}

locator& locator::operator=(const locator &a)
{
	val_ = a.val_;
	hash_ = a.hash_;
	hash1_ = a.hash1_;

	return *this;
}

locator::value::value(const locator::value& a) :
  type_(a.type_), filename_(a.filename_), loc_(a.loc_),
  modifications_(a.modifications_),
  center_x_(a.center_x_), center_y_(a.center_y_)
{
}

locator::value::value() :
	type_(NONE), filename_(), loc_(), modifications_(),
  center_x_(0), center_y_(0)

{}

locator::value::value(const char *filename) :
  type_(FILE), filename_(filename), loc_(), modifications_(),
  center_x_(0), center_y_(0)

{
}

locator::value::value(const std::string& filename) :
  type_(FILE), filename_(filename),  loc_(), modifications_(),
  center_x_(0), center_y_(0)

{
}

locator::value::value(const std::string& filename, const std::string& modifications) :
  type_(SUB_FILE), filename_(filename), loc_(), modifications_(modifications),
  center_x_(0), center_y_(0)

{
}

locator::value::value(const std::string& filename, const map_location& loc, int center_x, int center_y, const std::string& modifications) :
  type_(SUB_FILE), filename_(filename), loc_(loc), modifications_(modifications), center_x_(center_x), center_y_(center_y)
{
}

size_t hash_value(const locator::value& val) 
{
	using boost::hash_value;
	using boost::hash_combine;

	size_t hash = hash_value(val.type_);
	if (val.type_ == locator::FILE || val.type_ == locator::SUB_FILE) {
		hash_combine(hash, val.filename_);
	}
	if (val.type_ == locator::SUB_FILE) {
		hash_combine(hash, val.loc_.x);
		hash_combine(hash, val.loc_.y);
		hash_combine(hash, val.center_x_);
		hash_combine(hash, val.center_y_);
		hash_combine(hash, val.modifications_);
	}

	return hash;
}

size_t hash_value1(const locator::value& val) 
{
	// RLEHash
	size_t hash = 0;
	size_t x = 0;
	size_t len = val.filename_.size();
	const char* str = val.filename_.c_str();
	
	for	(size_t i = 0; i < len; str ++, i++) {    
		hash = (hash << 4) + (*str);         
		if ((x = hash & 0xF0000000L) != 0) {  
			hash ^= (x >> 24);
		}
		hash &= ~x;    
	}    
	return hash;
}

// Check if localized file is uptodate according to l10n track index.
// Make sure only that the image is not explicitly recorded as fuzzy,
// in order to be able to use non-tracked images (e.g. from UMC).
static std::set<std::string> fuzzy_localized_files;
static bool localized_file_uptodate (const std::string& loc_file)
{
	if (fuzzy_localized_files.size() == 0) {
		// First call, parse track index to collect fuzzy files by path.
		std::string fsep = "\xC2\xA6"; // UTF-8 for "broken bar"
		std::string trackpath = get_binary_file_location("", "l10n-track");
		std::string contents = read_file(trackpath);
		std::vector<std::string> lines = utils::split(contents, '\n');
		foreach (const std::string &line, lines) {
			size_t p1 = line.find(fsep);
			if (p1 == std::string::npos)
				continue;
			std::string state = line.substr(0, p1);
			utils::strip(state);
			if (state == "fuzzy") {
				size_t p2 = line.find(fsep, p1 + fsep.length());
				if (p2 == std::string::npos)
					continue;
				std::string relpath = line.substr(p1 + fsep.length(), p2 - p1 - fsep.length());
				fuzzy_localized_files.insert(game_config::path + '/' + relpath);
			}
		}
		fuzzy_localized_files.insert(""); // make sure not empty any more
	}
	return fuzzy_localized_files.count(loc_file) == 0;
}

// Return path to localized counterpart of the given file, if any, or empty string.
// Localized counterpart may also be requested to have a suffix to base name.
static std::string get_localized_path (const std::string& file, const std::string& suff = "")
{
	std::string dir = directory_name(file);
	std::string base = file_name(file);
	const size_t pos_ext = base.rfind(".");
	std::string loc_base;
	if (pos_ext != std::string::npos) {
		loc_base = base.substr(0, pos_ext) + suff + base.substr(pos_ext);
	} else {
		loc_base = base + suff;
	}
	// TRANSLATORS: This is the language code which will be used
	// to store and fetch localized non-textual resources, such as images,
	// when they exist. Normally it is just the code of the PO file itself,
	// e.g. "de" of de.po for German. But it can also be a comma-separated
	// list of language codes by priority, when the localized resource
	// found for first of those languages will be used. This is useful when
	// two languages share sufficient commonality, that they can use each
	// other's resources rather than duplicating them. For example,
	// Swedish (sv) and Danish (da) are such, so Swedish translator could
	// translate this message as "sv,da", while Danish as "da,sv".
	std::vector<std::string> langs = utils::split(_("language code for localized resources^en_US"));
	// In case even the original image is split into base and overlay,
	// add en_US with lowest priority, since the message above will
	// not have it when translated.
	langs.push_back("en_US");
	foreach (const std::string &lang, langs) {
		std::string loc_file = dir + "l10n" + "/" + lang + "/" + loc_base;
		if (file_exists(loc_file) && localized_file_uptodate(loc_file)) {
			return loc_file;
		}
	}
	return "";
}

// Load overlay image and compose it with the original surface.
static void add_localized_overlay (const std::string& ovr_file, surface &orig_surf)
{
	surface ovr_surf = IMG_Load(ovr_file.c_str());
	if (ovr_surf.null()) {
		return;
	}
	SDL_Rect area;
	area.x = 0;
	area.y = 0;
	area.w = ovr_surf->w;
	area.h = ovr_surf->h;
	sdl_blit(ovr_surf, 0, orig_surf, &area);
}

surface locator::load_image_file() const
{
	surface res;

	std::string location = get_binary_file_location("images", val_.filename_);


	{
		if (!location.empty()) {
			// Check if there is a localized image.
			const std::string loc_location = get_localized_path(location);
			if (!loc_location.empty()) {
				location = loc_location;
			}
			res = IMG_Load(location.c_str());
			// If there was no standalone localized image, check if there is an overlay.
			if (!res.null() && loc_location.empty()) {
				const std::string ovr_location = get_localized_path(location, "--overlay");
				if (!ovr_location.empty()) {
					add_localized_overlay(ovr_location, res);
				}
			}
		}
	}

	if (res.null() && !val_.filename_.empty()) {
		ERR_DP << "could not open image '" << val_.filename_ << "'\n";
		if (game_config::debug && val_.filename_ != game_config::images::missing)
			return get_image(game_config::images::missing, UNSCALED);
	}

	return res;
}

surface locator::load_image_sub_file() const
{
	surface surf = get_image(val_.filename_, UNSCALED);
	if(surf == NULL)
		return NULL;

	if(val_.loc_.valid()) {
		SDL_Rect srcrect = create_rect(
				((tile_size*3) / 4) * val_.loc_.x
				, tile_size * val_.loc_.y + (tile_size / 2) * (val_.loc_.x % 2)
				, tile_size
				, tile_size);

		if(val_.center_x_ >= 0 && val_.center_y_>= 0){
			srcrect.x += surf->w/2 - val_.center_x_;
			srcrect.y += surf->h/2 - val_.center_y_;
		}

		if ((srcrect.x + tile_size <= 0) || (srcrect.y + tile_size <= 0)) {
			add_to_cache(is_empty_hex_, true);
			return NULL;
		}

		surface cut(cut_surface(surf, srcrect));
		bool is_empty = false;
		surf = mask_surface(cut, get_hexmask(), &is_empty);
		add_to_cache(is_empty_hex_, is_empty);
	}

	if(val_.modifications_.size()){
		// The RC functor is very special; it must be applied
		// before anything else, and it is not accumulative.
		rc_function rc;
		// The FL functor is delayed until the end of the sequence.
		// This allows us to ignore things like ~FL(horiz)~FL(horiz)
		fl_function fl;
		// Regular functors
		std::vector< image::function_base* > functor_queue;

		const std::vector<std::string> modlist = utils::parenthetical_split(val_.modifications_,'~');

		foreach(const std::string& s, modlist) {
			const std::vector<std::string> tmpmod = utils::parenthetical_split(s);
			std::vector<std::string>::const_iterator j = tmpmod.begin();
			while(j!= tmpmod.end()){
				const std::string function = *j++;
				if(j == tmpmod.end()){
					if(function.size()){
						ERR_DP << "error parsing image modifications: "
							<< val_.modifications_<< "\n";
					}
					break;
				}
				const std::string field = *j++;
				typedef std::pair<Uint32,Uint32> rc_entry_type;

				// Team color (TC), a subset of RC's functionality
				if("TC" == function) {
					std::vector<std::string> param = utils::split(field,',');
					if(param.size() < 2) {
						ERR_DP << "too few arguments passed to the ~TC() function\n";
						break;
					}

					int side_n = lexical_cast_default<int>(param[0], -1);
					std::string team_color;
					if (side_n < 1) {
						ERR_DP << "invalid team (" << side_n << ") passed to the ~TC() function\n";
						break;
					}
					else if (side_n < static_cast<int>(team_colors.size())) {
						team_color = team_colors[side_n - 1];
					}
					else {
						// This side is not initialized; use default "n"
						try {
							team_color = lexical_cast<std::string>(side_n);
						} catch(bad_lexical_cast const&) {
							ERR_DP << "bad things happen\n";
						}
					}

					//
					// Pass parameters for RC functor
					//
					if(game_config::tc_info(param[1]).size()){
						std::map<Uint32, Uint32> tmp_map;
						try {
							color_range const& new_color =
								game_config::color_info(team_color);
							std::vector<Uint32> const& old_color =
								game_config::tc_info(param[1]);

							tmp_map = recolor_range(new_color,old_color);
						}
						catch(config::error const& e) {
							ERR_DP
								<< "caught config::error while processing TC: "
								<< e.message
								<< '\n';
							ERR_DP
								<< "bailing out from TC\n";
							tmp_map.clear();
						}

						foreach(const rc_entry_type& rc_entry, tmp_map) {
							rc.map()[rc_entry.first] = rc_entry.second;
						}
					}
					else {
						ERR_DP
							<< "could not load TC info for '" << param[1] << "' palette\n";
						ERR_DP
							<< "bailing out from TC\n";
					}

				}
				// Palette recolor (RC)
				else if("RC" == function) {
					const std::vector<std::string> recolor_params = utils::split(field,'>');
					if(recolor_params.size()>1){
						//
						// recolor source palette to color range
						//
						std::map<Uint32, Uint32> tmp_map;
						try {
							color_range const& new_color =
								game_config::color_info(recolor_params[1]);

							std::vector<Uint32> const& old_color =
								game_config::tc_info(recolor_params[0]);

							tmp_map = recolor_range(new_color,old_color);
						}
						catch (config::error& e) {
							ERR_DP
								<< "caught config::error while processing color-range RC: "
								<< e.message
								<< '\n';
							ERR_DP
								<< "bailing out from RC\n";
							tmp_map.clear();
						}

						foreach(const rc_entry_type& rc_entry, tmp_map) {
							rc.map()[rc_entry.first] = rc_entry.second;
						}
					}
					else {
						///@Deprecated 1.6 palette switch syntax
						if(field.find('=') != std::string::npos) {
							lg::wml_error << "the ~RC() image function cannot be used for palette switch (A=B) in 1.7.x; use ~PAL(A>B) instead\n";
						}
					}
				}
				// Palette switch (PAL)
				else if("PAL" == function) {
					const std::vector<std::string> remap_params = utils::split(field,'>');
					if(remap_params.size() > 1) {
						std::map<Uint32, Uint32> tmp_map;
						try {
							std::vector<Uint32> const& old_palette =
								game_config::tc_info(remap_params[0]);
							std::vector<Uint32> const& new_palette =
								game_config::tc_info(remap_params[1]);

							for(size_t i = 0; i < old_palette.size() && i < new_palette.size(); ++i) {
								tmp_map[old_palette[i]] = new_palette[i];
							}
						}
						catch(config::error& e) {
							ERR_DP
								<< "caught config::error while processing PAL function: "
								<< e.message
								<< '\n';
							ERR_DP
								<< "bailing out from PAL\n";
							tmp_map.clear();
						}

						foreach(const rc_entry_type& rc_entry, tmp_map) {
							rc.map()[rc_entry.first] = rc_entry.second;
						}
					}
				}
				// Flip-flop (FL)
				else if("FL" == function) {
					if(field.empty() || field.find("horiz") != std::string::npos) {
						fl.toggle_horiz();
					}
					if(field.find("vert") != std::string::npos) {
						fl.toggle_vert();
					}
				}
				// Grayscale (GS)
				else if("GS" == function) {
					functor_queue.push_back(new gs_function());
				}
				// Color-shift (CS)
				else if("CS" == function) {
					std::vector<std::string> const factors = utils::split(field, ',');
					const size_t s = factors.size();
					if(s) {
						int r = 0, g = 0, b = 0;

						r = lexical_cast_default<int>(factors[0]);
						if( s > 1 ) {
							g = lexical_cast_default<int>(factors[1]);
						}
						if( s > 2 ) {
							b = lexical_cast_default<int>(factors[2]);
						}

						functor_queue.push_back(new cs_function(r,g,b));
					}
					else {
						LOG_DP << "no arguments passed to the ~CS() function\n";
					}
				}
				// Crop/slice (CROP)
				else if("CROP" == function) {
					std::vector<std::string> const& slice_params = utils::split(field, ',', utils::STRIP_SPACES);
					const size_t s = slice_params.size();
					if(s) {
						SDL_Rect slice_rect = { 0, 0, 0, 0 };

						slice_rect.x = lexical_cast_default<Sint16, const std::string&>(slice_params[0]);
						if(s > 1) {
							slice_rect.y = lexical_cast_default<Sint16, const std::string&>(slice_params[1]);
						}
						if(s > 2) {
							slice_rect.w = lexical_cast_default<Uint16, const std::string&>(slice_params[2]);
						}
						if(s > 3) {
							slice_rect.h = lexical_cast_default<Uint16, const std::string&>(slice_params[3]);
						}

						functor_queue.push_back(new crop_function(slice_rect));
					}
					else {
						ERR_DP << "no arguments passed to the ~CROP() function\n";
					}
				}
				// LOC function
				else if("LOC" == function) {
					//FIXME: WIP, don't use it yet
					std::vector<std::string> const& params = utils::split(field);
					int x = lexical_cast<int>(params[0]);
					int y = lexical_cast<int>(params[1]);
					int cx = lexical_cast<int>(params[2]);
					int cy = lexical_cast<int>(params[3]);
					image::locator new_loc(val_.filename_, map_location(x,y), cx, cy, "");//TODO remove only ~LOC
					surf = get_image(new_loc, TOD_COLORED);
				}
				// BLIT function
				else if("BLIT" == function) {
					std::vector<std::string> param = utils::parenthetical_split(field, ',');
					const size_t s = param.size();
					if(s > 0){
						int x = 0, y = 0;
						if(s == 3) {
							x = lexical_cast_default<int>(param[1]);
							y = lexical_cast_default<int>(param[2]);
						}
						if(x >= 0 && y >= 0) { //required by blit_surface
							surface surf = get_image(param[0]);
							functor_queue.push_back(new blit_function(surf, x, y));
						} else {
							ERR_DP << "negative position arguments in ~BLIT() function\n";
						}
					} else {
						ERR_DP << "no arguments passed to the ~BLIT() function\n";
					}
				}
				else if("MASK" == function) {
					std::vector<std::string> param = utils::parenthetical_split(field, ',');
					const size_t s = param.size();
					if(s > 0){
						int x = 0, y = 0;
						if(s == 3) {
							x = lexical_cast_default<int>(param[1]);
							y = lexical_cast_default<int>(param[2]);
						}
						if(x >= 0 && y >= 0) { //required by blit_surface
							surface surf = get_image(param[0]);
							functor_queue.push_back(new mask_function(surf, x, y));
						} else {
							ERR_DP << "negative position arguments in ~MASK() function\n";
						}
					} else {
						ERR_DP << "no arguments passed to the ~MASK() function\n";
					}
				}
				else if("L" == function) {
					if(!field.empty()){
						surface surf = get_image(field);
						functor_queue.push_back(new light_function(surf));
					} else {
						ERR_DP << "no arguments passed to the ~L() function\n";
					}
				}
				// Scale (SCALE)
				else if("SCALE" == function) {
					std::vector<std::string> const& scale_params = utils::split(field, ',', utils::STRIP_SPACES);
					const size_t s = scale_params.size();
					if(s) {
						int w = 0, h = 0;

						w = lexical_cast_default<int, const std::string&>(scale_params[0]);
						if(s > 1) {
							h = lexical_cast_default<int, const std::string&>(scale_params[1]);
						}

						functor_queue.push_back(new scale_function(w, h));
					}
					else {
						ERR_DP << "no arguments passed to the ~SCALE() function\n";
					}
				}
				// Gaussian-like blur (BL)
				else if("BL" == function) {
					const int depth = std::max<int>(0, lexical_cast_default<int>(field));
					functor_queue.push_back(new bl_function(depth));
				}
				// Opacity-shift (O)
				else if("O" == function) {
					const std::string::size_type p100_pos = field.find('%');
					float num = 0.0f;
					if(p100_pos == std::string::npos)
						num = lexical_cast_default<float,const std::string&>(field);
					else {
						// make multiplier
						const std::string parsed_field = field.substr(0, p100_pos);
						num = lexical_cast_default<float,const std::string&>(parsed_field);
						num /= 100.0f;
					}
					functor_queue.push_back(new o_function(num));
				}
				//
				// ~R(), ~G() and ~B() are the children of ~CS(). Merely syntatic sugar.
				// Hence they are at the end of the evaluation.
				//
				// Red component color-shift (R)
				else if("R" == function) {
					const int r = lexical_cast_default<int>(field);
					functor_queue.push_back(new cs_function(r,0,0));
				}
				// Green component color-shift (G)
				else if("G" == function) {
					const int g = lexical_cast_default<int>(field);
					functor_queue.push_back(new cs_function(0,g,0));
				}
				// Blue component color-shift (B)
				else if("B" == function) {
					const int b = lexical_cast_default<int>(field);
					functor_queue.push_back(new cs_function(0,0,b));
				}
				else if("NOP" == function) {
				}
				// Fake image function used by GUI2 portraits until
				// Mordante gets rid of it. *tsk* *tsk*
				else if("RIGHT" == function) {
				}
				// Add a bright overlay.
				else if (function == "BRIGHTEN") {
					functor_queue.push_back(new brighten_function());
				}
				// Add a dark overlay.
				else if (function == "DARKEN") {
					functor_queue.push_back(new darken_function());
				}
				else {
					ERR_DP << "unknown image function in path: " << function << '\n';
				}
			}
		}

		if(!rc.no_op()) {
			surf = rc(surf);
		}

		if(!fl.no_op()) {
			surf = fl(surf);
		}

		foreach(function_base* f, functor_queue) {
			surf = (*f)(surf);
			delete f;
		}
	}

	return surf;
}

bool locator::file_exists()
{
	return !get_binary_file_location("images", val_.filename_).empty();
}

surface locator::load_from_disk() const
{
	switch(val_.type_) {
		case FILE:
			return load_image_file();
		case SUB_FILE:
			return load_image_sub_file();
		default:
			return surface(NULL);
	}
}


manager::manager() {}

manager::~manager()
{
	flush_cache();
}

SDL_PixelFormat last_pixel_format;

void set_pixel_format(SDL_PixelFormat* format)
{
	assert(format != NULL);

	SDL_PixelFormat &f = *format;
	SDL_PixelFormat &l = last_pixel_format;
	// if the pixel format change, we clear the cache,
	// because some images are now optimized for the wrong display format
	// FIXME: 8 bpp use palette, need to compare them. For now assume a change
	if (format->BitsPerPixel == 8 ||
		f.BitsPerPixel != l.BitsPerPixel || f.BytesPerPixel != l.BytesPerPixel ||
		f.Rmask != l.Rmask || f.Gmask != l.Gmask || f.Bmask != l.Bmask ||
		f.Rloss != l.Rloss || f.Gloss != l.Gloss || f.Bloss != l.Bloss ||
		f.Rshift != l.Rshift || f.Gshift != l.Gshift || f.Bshift != l.Bshift ||
		f.Amask != l.Amask || f.Aloss != l.Aloss || f.Ashift != l.Ashift)
	{
		LOG_DP << "detected a new display format\n";
		flush_cache();
	}
	last_pixel_format = *format;
}

void set_color_adjustment(int r, int g, int b)
{
	if(r != red_adjust || g != green_adjust || b != blue_adjust) {
		red_adjust = r;
		green_adjust = g;
		blue_adjust = b;
		tod_colored_images_.flush();
		brightened_images_.flush();
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
		semi_brightened_images_.flush();
#endif
		reversed_images_.clear();
	}
}

color_adjustment_resetter::color_adjustment_resetter()
: r_(red_adjust), g_(green_adjust), b_(blue_adjust)
{
}

void color_adjustment_resetter::reset()
{
	set_color_adjustment(r_, g_, b_);
}

void set_team_colors(const std::vector<std::string>* colors)
{
	if (colors == NULL)
		team_colors.clear();
	else {
		team_colors = *colors;
	}
}

std::vector<std::string>& get_team_colors()
{
	return team_colors;
}

void set_zoom(int amount)
{
	if(amount != zoom) {
		zoom = amount;
		tod_colored_images_.flush();
		brightened_images_.flush();
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
		semi_brightened_images_.flush();
#endif
		reversed_images_.clear();

		// We keep these caches if:
		// we use default zoom (it doesn't need those)
		// or if they are already at the wanted zoom.
		scaled_to_hex_images_.flush();
		if (zoom != tile_size && zoom != cached_zoom) {
			scaled_to_zoom_.flush();
			// scaled_to_hex_images_.flush();
			cached_zoom = zoom;
		}
	}
}

surface get_hexed(const locator& i_locator)
{
	surface image(get_image(i_locator, UNSCALED));
	if (!i_locator.get_loc().valid()) {
		// Re-cut scaled tiles according to a mask.
		bool is_empty = false;
		surface res = mask_surface(image, get_hexmask(), &is_empty);
		i_locator.add_to_cache(is_empty_hex_, is_empty);
		if (!is_empty)
			res = create_optimized_surface(res);
		return res;
	}
	return image;
}

static surface get_scaled_to_hex(const locator& i_locator)
{
	// surface img = get_image(i_locator, HEXED);
	surface img = get_hexed(i_locator);
	if (zoom == tile_size) {
		return img;
	} else {
		return scale_surface(img, zoom, zoom);
	}
}

static surface get_tod_colored(const locator& i_locator)
{
	surface img = get_image(i_locator, SCALED_TO_HEX);
	return adjust_surface_color(img, red_adjust, green_adjust, blue_adjust);
}

static surface get_scaled_to_zoom(const locator& i_locator)
{
	assert(zoom != tile_size);
	assert(tile_size != 0);

	surface res(get_image(i_locator, UNSCALED));
	// For some reason haloes seems to have invalid images, protect against crashing
	if(!res.null()) {
		return scale_surface(res, ((res.get()->w * zoom) / tile_size), ((res.get()->h * zoom) / tile_size));
	} else {
		return surface(NULL);
	}
}

static surface get_brightened(const locator& i_locator)
{
	surface image(get_image(i_locator, TOD_COLORED));
	return surface(brighten_image(image, ftofxp(game_config::hex_brightening)));
}

static surface get_semi_brightened(const locator& i_locator)
{
	surface image(get_image(i_locator, TOD_COLORED));
	return surface(brighten_image(image, ftofxp(game_config::hex_semi_brightening)));
}

///translate type to a simpler one when possible
static TYPE simplify_type(const image::locator& i_locator, TYPE type){
	switch(type) {
	case SCALED_TO_ZOOM:
		if(zoom == tile_size)
			type = UNSCALED;
		break;
	case BRIGHTENED:
		if(ftofxp(game_config::hex_brightening) == ftofxp(1.0))
			type = TOD_COLORED;
		break;
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
	case SEMI_BRIGHTENED:
		if(ftofxp(game_config::hex_semi_brightening) == ftofxp(1.0))
			type = TOD_COLORED;
		break;
#endif
	default:
		break;
	}

	if(type == TOD_COLORED) {
		if (red_adjust==0 && green_adjust==0 && blue_adjust==0)
			type = SCALED_TO_HEX;
	}
/*
	if(type == SCALED_TO_HEX) {
		if(zoom == tile_size)
			type = HEXED;
	}
*/
	if(type == HEXED) {
		// check if the image is already hex-cut by the location system
		if(i_locator.get_loc().valid())
			type = UNSCALED;
	}

	return type;
}


surface get_image(const image::locator& i_locator, TYPE type)
{
	surface res;
	int index;

	if (i_locator.is_void()) {
		return res;
	}

	type = simplify_type(i_locator, type);

	image_cache *imap;
	// select associated cache
	switch(type) {
	case UNSCALED:
		imap = &images_;
		break;
	case TOD_COLORED:
		imap = &tod_colored_images_;
		break;
	case SCALED_TO_ZOOM:
		imap = &scaled_to_zoom_;
		break;
	case HEXED:
		// imap = &hexed_images_;
		break;
	case SCALED_TO_HEX:
		imap = &scaled_to_hex_images_;
		break;
	case BRIGHTENED:
		imap = &brightened_images_;
		break;
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
	case SEMI_BRIGHTENED:
		imap = &semi_brightened_images_;
		break;
#endif
	default:
		return res;
	}

	// return the image if already cached
	if ((index = i_locator.in_cache(*imap)) >= 0)
		return i_locator.locate_in_cache(*imap, index);

	// not cached, generate it
	switch(type) {
	case UNSCALED:
		// If type is unscaled, directly load the image from the disk.
		res = i_locator.load_from_disk();
		break;
	case TOD_COLORED:
		res = get_tod_colored(i_locator);
		break;
	case SCALED_TO_ZOOM:
		res = get_scaled_to_zoom(i_locator);
		break;
	case HEXED:
		res = get_hexed(i_locator);
		break;
	case SCALED_TO_HEX:
		res = get_scaled_to_hex(i_locator);
		break;
	case BRIGHTENED:
		res = get_brightened(i_locator);
		break;
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
	case SEMI_BRIGHTENED:
		res = get_semi_brightened(i_locator);
		break;
#endif
	default:
		return res;
	}

	// Optimizes surface before storing it
	if (res)
		res = create_optimized_surface(res);

	i_locator.add_to_cache(*imap, res);

	return res;
}

surface get_hexmask()
{
	static const image::locator terrain_mask(game_config::images::terrain_mask);
	return get_image(terrain_mask, UNSCALED);
}

bool is_in_hex(const locator& i_locator)
{
	int index;
	if ((index = i_locator.in_cache(in_hex_info_)) >= 0) {
		return i_locator.locate_in_cache(in_hex_info_, index);
	} else {
		const surface image(get_image(i_locator, UNSCALED));

		bool res = in_mask_surface(image, get_hexmask());

		i_locator.add_to_cache(in_hex_info_, res);

		//std::cout << "in_hex : " << i_locator.get_filename()
		//		<< " " << (res ? "yes" : "no") << "\n";

		return res;
	}
}

bool is_empty_hex(const locator& i_locator)
{
	int index;
	if ((index = i_locator.in_cache(is_empty_hex_)) < 0) {
		// const surface surf = get_image(i_locator, HEXED);
		const surface surf = get_image(i_locator);
		// emptiness of terrain image is checked during hex cut
		// so, maybe in cache now, let's recheck
		if ((index = i_locator.in_cache(is_empty_hex_)) < 0) {
			//should never reach here
			//but do it manually if it happens
			//assert(false);
			bool is_empty = false;
			mask_surface(surf, get_hexmask(), &is_empty);
			i_locator.add_to_cache(is_empty_hex_, is_empty);
			return is_empty;
		}
	}
	return i_locator.locate_in_cache(is_empty_hex_, index);
}

surface reverse_image(const surface& surf)
{
	if(surf == NULL) {
		return surface(NULL);
	}

	const std::map<surface,surface>::iterator itor = reversed_images_.find(surf);
	if(itor != reversed_images_.end()) {
		// sdl_add_ref(itor->second);
		return itor->second;
	}

	const surface rev(flip_surface(surf));
	if(rev == NULL) {
		return surface(NULL);
	}

	reversed_images_.insert(std::pair<surface,surface>(surf,rev));
	// sdl_add_ref(rev);
	return rev;
}

bool exists(const image::locator& i_locator)
{
	typedef image::locator loc;
	loc::type type = i_locator.get_type();
	if (type != loc::FILE && type != loc::SUB_FILE)
		return false;

	// The insertion will fail if there is already an element in the cache
	std::pair< std::map< std::string, bool >::iterator, bool >
		it = image_existence_map.insert(std::make_pair(i_locator.get_filename(), false));
	bool &cache = it.first->second;
	if (it.second)
		cache = !get_binary_file_location("images", i_locator.get_filename()).empty();
	return cache;
}

static void precache_file_existence_internal(const std::string& dir, const std::string& subdir)
{
	const std::string checked_dir = dir + "/" + subdir;
	if (precached_dirs.find(checked_dir) != precached_dirs.end())
		return;
	precached_dirs.insert(checked_dir);

	std::vector<std::string> files_found;
	std::vector<std::string> dirs_found;
	get_files_in_dir(checked_dir, &files_found, &dirs_found,
			FILE_NAME_ONLY, NO_FILTER, DONT_REORDER);

	for(std::vector<std::string>::const_iterator f = files_found.begin();
			f != files_found.end(); ++f) {
		image_existence_map[subdir + *f] = true;
	}

	for(std::vector<std::string>::const_iterator d = dirs_found.begin();
			d != dirs_found.end(); ++d) {
		precache_file_existence_internal(dir, subdir + *d + "/");
	}
}

void precache_file_existence(const std::string& subdir)
{
	const std::vector<std::string>& paths = get_binary_paths("images");

	for(std::vector<std::string>::const_iterator p = paths.begin();
			 p != paths.end(); ++p) {

		const std::string dir = *p + "/" + subdir;
		precache_file_existence_internal(*p, subdir);
	}
}

bool precached_file_exists(const std::string& file)
{
	std::map<std::string, bool>::const_iterator b =  image_existence_map.find(file);
	if (b != image_existence_map.end())
		return b->second;
	else
		return false;
}

} // end namespace image

