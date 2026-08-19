/*
 *  QuickSave.h - a manager for auto-named saved games
 
	Copyright (C) 2014 and beyond by Jeremiah Morris
	and the "Aleph One" developers.
 
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.
 
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
 
	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html
 
 */

#ifndef QUICK_SAVE_H
#define QUICK_SAVE_H

#include "FileHandler.h"
#include <string>
#include <vector>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <functional>
#endif

struct QuickSave {
    FileSpecifier save_file;
    std::string name;
    std::string level_name;
    time_t save_time;
    std::string formatted_time;
    int32 ticks;
    std::string formatted_ticks;
    int16 players;

    bool operator<(const QuickSave& other) const {
        return save_time < other.save_time;
    }
};

class QuickSaves {
    friend class QuickSaveLoader;
public:
    static QuickSaves* instance();
    typedef std::vector<QuickSave>::iterator iterator;
    
    void enumerate();
    void clear();
    void delete_surplus_saves(size_t max_saves);
    
    iterator begin() { return m_saves.begin(); }
    iterator end() { return m_saves.end(); }
    
private:
    QuickSaves() { }
    void add(const QuickSave& save) { m_saves.push_back(save); }
    
    std::vector<QuickSave> m_saves;
};

bool create_quick_save(void);
bool delete_quick_save(QuickSave& save);
#ifdef __EMSCRIPTEN__
// Web port (see ../../WEB_PORT_PLAN.md, M4h): this dialog blocks the whole
// tab under Emscripten (see sdl_dialogs.cpp) -- runs cooperatively instead,
// calling on_result once the user has actually made a choice (or
// cancelled) instead of returning a bool synchronously. saved_game is
// written before on_result fires if a save was chosen; the caller must
// keep it alive until then (e.g. heap-allocated), since this function
// itself returns immediately.
void load_quick_save_dialog(FileSpecifier& saved_game, std::function<void(bool)> on_result);
// Web port: runs a completed file-picker result on the frame loop rather than
// in the browser callback that produced it -- see the definition.
void update_pending_file_pick(void);
#else
bool load_quick_save_dialog(FileSpecifier& saved_game);
#endif
size_t saved_game_was_networked(FileSpecifier& saved_game);

#endif
