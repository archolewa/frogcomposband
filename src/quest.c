#include "angband.h"
#include "grid.h"

#include <assert.h>

doc_ptr trace_doc = NULL;

static cptr _strcpy(cptr s)
{
    char *r = malloc(strlen(s)+1);
    strcpy(r, s);
    return r;
}

/************************************************************************
 * Quest
 ***********************************************************************/
quest_ptr quest_alloc(cptr name)
{
    quest_ptr q = malloc(sizeof(quest_t));
    memset(q, 0, sizeof(quest_t));
    assert(name);
    q->name = _strcpy(name);
    return q;
}

void quest_free(quest_ptr q)
{
    if (q)
    {
        free((vptr)q->name);
        if (q->file) free((vptr)q->file);
        free(q);
    }
}

void quest_change_file(quest_ptr q, cptr file)
{
    assert(q);
    if (q->file) free((vptr)q->file);
    if (file) q->file = _strcpy(file);
    else q->file = NULL;
}

/************************************************************************
 * Quest Status
 ***********************************************************************/
void quest_take(quest_ptr q)
{
    string_ptr s;
    assert(q);
    assert(q->status == QS_UNTAKEN);
    q->status = QS_TAKEN;
    q->seed = randint0(0x10000000);
    s = quest_get_description(q);
    msg_format("<color:R>%s</color> (<color:U>Level %d</color>): %s",
        q->name, q->level, string_buffer(s));
    string_free(s);
}

void quest_complete(quest_ptr q, point_t p)
{
    assert(q);
    assert(q->status == QS_IN_PROGRESS);
    q->status = QS_COMPLETED;
    q->completed_lev = p_ptr->lev;

    virtue_add(VIRTUE_VALOUR, 2);
    p_ptr->fame += randint1(2);
    if (q->id == QUEST_OBERON || q->id == QUEST_SERPENT)
        p_ptr->fame += 50;

    if (!(q->flags & QF_NO_MSG))
        cmsg_print(TERM_L_BLUE, "You just completed your quest!");
    msg_add_tiny_screenshot(50, 24);

    /* create stairs before the reward */
    if (q->dungeon)
    {
        int x = p.x;
        int y = p.y;
        int nx,ny;

        while (cave_perma_bold(y, x) || cave[y][x].o_idx || (cave[y][x].info & CAVE_OBJECT) )
        {
            scatter(&ny, &nx, y, x, 1, 0);
            y = ny; x = nx;
        }

        cmsg_print(TERM_L_BLUE, "A magical staircase appears...");
        cave_set_feat(y, x, feat_down_stair);
        p_ptr->update |= PU_FLOW;
    }
    if (!(q->flags & QF_TOWN)) /* non-town quest get rewarded immediately */
    {
        int i, ct = dun_level/15 + 1;
        for (i = 0; i < ct; i++)
        {
            obj_t forge = {0};
            if (make_object(&forge, AM_GOOD | AM_GREAT | AM_TAILORED))
                drop_near(&forge, -1, p.y, p.x);
            else
                msg_print("Software Bug ... you missed out on your reward!");
        }
        if (no_wilderness)
            gain_chosen_stat();

        q->status = QS_FINISHED;
    }
    p_ptr->redraw |= PR_DEPTH;
}

void quest_reward(quest_ptr q)
{
    string_ptr s;
    obj_ptr reward;

    assert(q);
    assert(q->status == QS_COMPLETED);

    s = quest_get_description(q);
    msg_format("<color:R>%s</color> (<color:U>Level %d</color>): %s",
        q->name, q->level, string_buffer(s));
    string_free(s);

    reward = quest_get_reward(q);
    if (reward)
    {
        char name[MAX_NLEN];
        obj_identify_fully(reward);
        object_desc(name, reward, OD_COLOR_CODED);
        msg_format("You receive %s as a reward.", name);
        pack_carry(reward);
        obj_free(reward);
    }

    q->status = QS_FINISHED;
}

void quest_fail(quest_ptr q)
{
    assert(q);
    q->status = QS_FAILED;
    q->completed_lev = p_ptr->lev;
    msg_format("You have <color:v>failed</color> the quest: <color:R>%s</color>.", q->name);
    virtue_add(VIRTUE_VALOUR, -2);
    fame_on_failure();
    if (!(q->flags & QF_TOWN))
        q->status = QS_FAILED_DONE;
}

/************************************************************************
 * Quest Info from q->file
 ***********************************************************************/
static string_ptr _temp_desc;
static errr _parse_desc(char *line, int options)
{
    if (line[0] == 'D' && line[1] == ':')
    {
        if (string_length(_temp_desc) && string_get_last(_temp_desc) != ' ')
            string_append_c(_temp_desc, ' ');
        string_append_s(_temp_desc, line + 2);
    }
    return 0;
}
string_ptr quest_get_description(quest_ptr q)
{
    string_ptr s = string_alloc();
    if (q->file)
    {
        _temp_desc = s;
        parse_edit_file(q->file, _parse_desc, 0);
        _temp_desc = NULL;
    }
    return s;
}

static room_grid_ptr _temp_reward;
static errr _parse_reward(char *line, int options)
{
    if (line[0] == 'R' && line[1] == ':')
    {
        /* It is common to set a default reward, and then
         * to over-write it later for class specific rewards.
         * The last reward line wins. */
        memset(_temp_reward, 0, sizeof(room_grid_t));
        return parse_room_grid(line + 2, _temp_reward, options);
    }
    return 0;
}
obj_ptr quest_get_reward(quest_ptr q)
{
    obj_ptr reward = NULL;
    if (q->file)
    {
        room_grid_ptr letter = malloc(sizeof(room_grid_t));
        memset(letter, 0, sizeof(room_grid_t));
        _temp_reward = letter;
        if (parse_edit_file(q->file, _parse_reward, 0) == ERROR_SUCCESS)
        {
            if (!letter->object_level)
                letter->object_level = 1; /* Hack: force at least AM_GOOD */
            reward = room_grid_make_obj(letter, q->level);
        }
        _temp_reward = NULL;
        free(letter);
    }
    return reward;
}

static room_ptr _temp_room;
static errr _parse_room(char *line, int options)
{
    assert(_temp_room);
    return parse_room_line(_temp_room, line, options);
}
room_ptr quest_get_map(quest_ptr q)
{
    if (q->file && (q->flags & QF_GENERATE))
    {
        room_ptr room = room_alloc(q->name);
        _temp_room = room;
        if (parse_edit_file(q->file, _parse_room, 0) != ERROR_SUCCESS)
        {
            room_free(room);
            room = NULL;
        }
        _temp_room = NULL;
        return room;
    }
    return NULL;
}

/************************************************************************
 * Quest Levels
 ***********************************************************************/
static void _generate(room_ptr room)
{
    transform_ptr xform = transform_alloc_room(room, size(MAX_WID, MAX_HGT));
    int           panels_x, panels_y, x, y;

    /* figure out the dungeon size ... not sure if we need the panels crap */
    panels_y = xform->dest.cy / SCREEN_HGT;
    if (xform->dest.cy % SCREEN_HGT) panels_y++;
    cur_hgt = panels_y * SCREEN_HGT;

    panels_x = (xform->dest.cx / SCREEN_WID);
    if (xform->dest.cx % SCREEN_WID) panels_x++;
    cur_wid = panels_x * SCREEN_WID;

    /* Start with perm walls */
    for (y = 0; y < cur_hgt; y++)
    {
        for (x = 0; x < cur_wid; x++)
        {
            place_solid_perm_bold(y, x);
        }
    }

    /* generate the level */
    get_mon_num_prep(get_monster_hook(), NULL);
    build_room_template_aux(room, xform, NULL);
    transform_free(xform);
}
void quest_generate(quest_ptr q)
{
    room_ptr room;
    assert(q);
    assert(q->flags & QF_GENERATE);
    room = quest_get_map(q);
    assert(room);
    assert(vec_length(room->map));

    base_level = q->level;
    dun_level = base_level;
    object_level = base_level;
    monster_level = base_level;

    _generate(room);
    room_free(room);
}

bool quest_post_generate(quest_ptr q)
{
    assert(q);
    assert(q->status == QS_IN_PROGRESS);
    if (q->goal == QG_KILL_MON)
    {
        monster_race *r_ptr = &r_info[q->goal_idx];
        int           mode = PM_NO_KAGE | PM_NO_PET | PM_QUESTOR, i, j, k;
        int           ct = q->goal_count - q->goal_current;

        if ((r_ptr->flags1 & RF1_UNIQUE) && r_ptr->max_num == 0)
        {
            msg_print("It seems that this level was protected by someone before...");
            q->status = QS_FINISHED;
            return TRUE;
        }

        if (!(r_ptr->flags1 & RF1_FRIENDS))
            mode |= PM_ALLOW_GROUP; /* allow escorts but not friends */

        for (i = 0; i < ct; i++)
        {
            for (j = 1000; j > 0; j--)
            {
                int x = 0, y = 0;

                /* Find an empty grid */
                for (k = 1000; k > 0; k--)
                {
                    cave_type    *c_ptr;
                    feature_type *f_ptr;

                    y = randint0(cur_hgt);
                    x = randint0(cur_wid);

                    c_ptr = &cave[y][x];
                    f_ptr = &f_info[c_ptr->feat];

                    if (!have_flag(f_ptr->flags, FF_MOVE) && !have_flag(f_ptr->flags, FF_CAN_FLY)) continue;
                    if (!monster_can_enter(y, x, r_ptr, 0)) continue;
                    if (distance(y, x, py, px) < 10) continue;
                    if (c_ptr->info & CAVE_ICKY) continue;
                    else break;
                }

                /* No empty grids */
                if (!k) return FALSE;

                if (place_monster_aux(0, y, x, q->goal_idx, mode))
                {
                    m_list[hack_m_idx_ii].mflag2 |= MFLAG2_QUESTOR;
                    break;
                }
            }

            /* Failed to place */
            if (!j) return FALSE;
        }
        if (ct == 1)
            cmsg_format(TERM_VIOLET, "Beware, this level is protected by %s!", r_name + r_ptr->name);
        else
        {
            char name[MAX_NLEN];
            strcpy(name, r_name + r_ptr->name);
            plural_aux(name);
            cmsg_format(TERM_VIOLET, "Be warned, this level is guarded by %d %s!", ct, name);
        }
    }
    return TRUE;
}

/************************************************************************
 * Quests (cf q_info.txt)
 ***********************************************************************/
static int_map_ptr _quests = NULL;
static int         _current = 0;

static errr _parse_q_info(char *line, int options)
{
    static quest_ptr quest = NULL;

    /* N:1:5:Thieves' Hideout */
    if (line[0] == 'N' && line[1] == ':')
    {
        char *zz[10];
        int   num = tokenize(line + 2, 10, zz, 0);

        if (num != 3 || !*zz[2])
        {
            msg_print("Error: Invalid N: line. Syntax: N:id:lvl:name.");
            return PARSE_ERROR_TOO_FEW_ARGUMENTS;
        }

        quest = quest_alloc(zz[2]);
        quest->id = atoi(zz[0]);
        quest->level = atoi(zz[1]);
        int_map_add(_quests, quest->id, quest);
    }
    /* T:TOWN | GENERATE */
    else if (line[0] == 'T' && line[1] == ':')
    {
        char *flags[10];
        int   flag_ct = z_string_split(line + 2, flags, 10, "|");
        int   i;

        trim_tokens(flags, flag_ct);
        for (i = 0; i < flag_ct; i++)
        {
            char* flag = flags[i];

            if (streq(flag, "TOWN"))
                quest->flags |= QF_TOWN;
            else if (streq(flag, "GENERATE"))
                quest->flags |= QF_GENERATE;
            else if (streq(flag, "RETAKE"))
                quest->flags |= QF_RETAKE;
            else if (streq(flag, "RANDOM"))
                quest->flags |= QF_RANDOM;
            else if (streq(flag, "ANYWHERE"))
                quest->flags |= QF_ANYWHERE;
            else if (streq(flag, "NO_MSG"))
                quest->flags |= QF_NO_MSG;
            else
            {
                msg_format("Error: Invalid quest flag %s.", flag);
                return PARSE_ERROR_INVALID_FLAG;
            }
        }
    }
    /* F:q_thieves.txt */
    else if (line[0] == 'F' && line[1] == ':')
    {
        quest->file = _strcpy(line + 2);
    }
    /* G:KILL(logrus master)
     * G:KILL(archlich, 5)
     * G:FIND(^sting$) */
    else if (line[0] == 'G' && line[1] == ':')
    {
        char *name;
        char *args[10];
        int   arg_ct = parse_args(line + 2, &name, args, 10);

        if (arg_ct < 1)
        {
            msg_format("Error: Missing arguments to %s quest goal.", name);
            return PARSE_ERROR_TOO_FEW_ARGUMENTS;
        }

        if (streq(name, "KILL"))
        {
            quest->goal = QG_KILL_MON;
            quest->goal_idx = parse_lookup_monster(args[0], options);
            quest->goal_count = 1;
            if (arg_ct >= 2)
                quest->goal_count = atoi(args[1]);
        }
        else if (streq(name, "FIND"))
        {
            quest->goal = QG_FIND_ART;
            quest->goal_idx = parse_lookup_artifact(args[0], options);
        }
        else
        {
            msg_format("Error: Unkown quest goal %s. Try KILL(name[, ct]) or FIND(art).", name);
            return PARSE_ERROR_INVALID_FLAG;
        }
    }
    /* W:Stronghold */
    else if (line[0] == 'W' && line[1] == ':')
    {
        quest->dungeon = parse_lookup_dungeon(line + 2, options);
        if (!quest->dungeon)
        {
            msg_format("Error: Unkown dungeon %s. Consult d_info.txt.", line + 2);
            return PARSE_ERROR_INVALID_FLAG;
        }
    }
    else
        return PARSE_ERROR_UNDEFINED_DIRECTIVE;
    return 0;
}

bool quests_init(void)
{
    assert(!_quests);
    _quests = int_map_alloc((int_map_free_f)quest_free);
    return !parse_edit_file("q_info.txt", _parse_q_info, 0); /* errr -> bool */
}

void quests_add(quest_ptr q)
{
    assert(q);
    assert(q->id);
    int_map_add(_quests, q->id, q);
}

void quests_cleanup(void)
{
    int_map_free(_quests);
    _quests = NULL;
    _current = 0;
}

quest_ptr quests_get_current(void)
{
    if (!_current) return NULL;
    return int_map_find(_quests, _current);
}

quest_ptr quests_get(int id)
{
    return int_map_find(_quests, id);
}

cptr quests_get_name(int id)
{
    quest_ptr q = quests_get(id);
    if (!q) return "";
    return q->name;
}

static int _quest_cmp_level(quest_ptr l, quest_ptr r)
{
    if (l->level < r->level) return -1;
    if (l->level > r->level) return 1;
    if (l->completed_lev < r->completed_lev) return -1;
    if (l->completed_lev > r->completed_lev) return 1;
    if (l->id < r->id) return -1;
    if (l->id > r->id) return 1;
    return 0;
}

static vec_ptr _quests_get(quest_p p)
{
    vec_ptr v = vec_alloc(NULL);
    int_map_iter_ptr i;

    for (i = int_map_iter_alloc(_quests);
            int_map_iter_is_valid(i);
            int_map_iter_next(i))
    {
        quest_ptr q = int_map_iter_current(i);
        if (!p || p(q))
            vec_add(v, q);
    }
    int_map_iter_free(i);

    vec_sort(v, (vec_cmp_f)_quest_cmp_level);
    return v;
}


static bool _is_active(quest_ptr q) { return q->status == QS_TAKEN || q->status == QS_IN_PROGRESS || q->status == QS_COMPLETED; }
static bool _is_finished(quest_ptr q) { return q->status == QS_FINISHED; }
static bool _is_failed(quest_ptr q) { return q->status == QS_FAILED || q->status == QS_FAILED_DONE; }
static bool _is_hidden(quest_ptr q) { return (q->flags & QF_RANDOM) && q->status == QS_UNTAKEN; }
static bool _is_random(quest_ptr q) { return BOOL(q->flags & QF_RANDOM); }

vec_ptr quests_get_all(void) { return _quests_get(NULL); }
vec_ptr quests_get_active(void) { return _quests_get(_is_active); }
vec_ptr quests_get_finished(void) { return _quests_get(_is_finished); }
vec_ptr quests_get_failed(void) { return _quests_get(_is_failed); }
vec_ptr quests_get_hidden(void) { return _quests_get(_is_hidden); }
vec_ptr quests_get_random(void) { return _quests_get(_is_random); }

/************************************************************************
 * Quests: Randomize on Birth (from birth.c with slight mods)
 ***********************************************************************/
static bool _r_can_quest(int r_idx)
{
    monster_race *r_ptr = &r_info[r_idx];
    if (r_ptr->flags8 & RF8_WILD_ONLY) return FALSE;
    if (r_ptr->flags7 & RF7_AQUATIC) return FALSE;
    if (r_ptr->flags2 & RF2_MULTIPLY) return FALSE;
    if (r_ptr->flags7 & RF7_FRIENDLY) return FALSE;
    return TRUE;
}

static bool _r_is_unique(int r_idx) { return BOOL(r_info[r_idx].flags1 & RF1_UNIQUE); }
static bool _r_is_nonunique(int r_idx)
{
    monster_race *r_ptr = &r_info[r_idx];
    if (r_ptr->flags1 & RF1_UNIQUE) return FALSE;
    if (r_ptr->flags7 & RF7_UNIQUE2) return FALSE;
    if (r_ptr->flags7 & RF7_NAZGUL) return FALSE;
    return TRUE;
}
static void _get_questor(quest_ptr q)
{
    int           r_idx = 0;
    monster_race *r_ptr;
    int           attempt;
    bool          force_unique = FALSE;
    bool          prevent_unique = FALSE;

    /* High Level quests are stacked with uniques. Everything else
       is stacked the other way. So lets make some attempt at balance.
       Of course, users can force all quests to be for uniques, in
       true Hengband spirit. */
    if (quest_unique || one_in_(3))
    {
        get_mon_num_prep(_r_can_quest, _r_is_unique);
        force_unique = TRUE;
    }
    else if (one_in_(2))
    {
        get_mon_num_prep(_r_can_quest, _r_is_nonunique);
        prevent_unique = TRUE;
    }
    else
        get_mon_num_prep(_r_can_quest, NULL);

    for(attempt = 0;; attempt++)
    {
        int accept_lev = q->level + q->level / 20;
        int mon_lev = q->level + 5 + randint1(q->level / 10);

        /* Hacks for high level quests */
        if (accept_lev > 88)
            accept_lev = 88;

        if (mon_lev > 88)
            mon_lev = 88;

        unique_count = 0; /* Hack: get_mon_num assume level generation and restricts uniques per level */
        r_idx = get_mon_num(mon_lev);
        r_ptr = &r_info[r_idx];

        /* Try to enforce preferences, but its virtually impossible to prevent
           high level quests for uniques */
        if (attempt < 4000)
        {
            if (prevent_unique && (r_ptr->flags1 & RF1_UNIQUE)) continue;
            if (force_unique && !(r_ptr->flags1 & RF1_UNIQUE)) continue;
        }

        if (r_ptr->flags1 & RF1_QUESTOR) continue;
        if (r_ptr->flags1 & RF1_NO_QUEST) continue;
        if (r_ptr->rarity > 100) continue;
        if (r_ptr->flags7 & RF7_FRIENDLY) continue;
        if (r_ptr->flags7 & RF7_AQUATIC) continue;
        if (r_ptr->flags8 & RF8_WILD_ONLY) continue;
        if (r_ptr->level > q->level + 12) continue;
        if (r_ptr->level > accept_lev || attempt > 5000)
        {
            q->goal_idx = r_idx;
            if (r_ptr->flags1 & RF1_UNIQUE)
            {
                r_ptr->flags1 |= RF1_QUESTOR;
                q->goal_count = 1;
            }
            else
                q->goal_count = rand_range(10, 20);
            q->goal_current = 0;
            break;
        }
    }
}

void quests_on_birth(void)
{
    vec_ptr v;
    int i;

    /* stale data from the last character */
    v = quests_get_all();
    for (i = 0; i < vec_length(v); i++)
    {
        quest_ptr q = vec_get(v, i);
        q->status = QS_UNTAKEN;
        q->completed_lev = 0;
        q->goal_current = 0;
        q->seed = 0;
    }
    vec_free(v);
    _current = 0;

    /* assign random quests */
    v = quests_get_random();
    for (i = 0; i < vec_length(v); i++)
    {
        quest_ptr q = vec_get(v, i);
        if (q->goal == QG_KILL_MON)
        {
            if (q->goal_idx) r_info[q->goal_idx].flags1 &= ~RF1_QUESTOR;
            _get_questor(q);
        }
    }
    vec_free(v);

    /* take the standard fixed quests */
    quests_get(QUEST_OBERON)->status = QS_TAKEN;
    quests_get(QUEST_SERPENT)->status = QS_TAKEN;
}

/************************************************************************
 * Quests: Hooks
 ***********************************************************************/
void quests_on_leave_floor(void)
{
    quest_ptr q;
    if (!_current) return;
    q = quests_get(_current);
    assert(q);
    /* From floors.c ... I'm not quite sure what we are after here.
     * I suppose if the user flees an uncompleted quest for non-unique
     * monsters (e.g. Kill 16 Wargs) then, if they re-enter the quest
     * level, those quest monsters should not be present. Hey, they
     * missed their chance! It's weird that we let the uniques remain, though. */
    if (q->goal == QG_KILL_MON)
    {
        int i;
        for (i = 1; i < m_max; i++)
        {
            monster_race *r_ptr;
            monster_type *m_ptr = &m_list[i];

            /* Skip dead monsters */
            if (!m_ptr->r_idx) continue;

            /* Only maintain quest monsters */
            if (q->goal_idx != m_ptr->r_idx) continue;

            /* Extract real monster race */
            r_ptr = real_r_ptr(m_ptr);

            /* Ignore unique monsters */
            if ((r_ptr->flags1 & RF1_UNIQUE) ||
                (r_ptr->flags7 & RF7_NAZGUL)) continue;

            /* Delete non-unique quest monsters */
            delete_monster_idx(i);
        }
    }
}

static int _quest_dungeon(quest_ptr q)
{
    int d = q->dungeon;
    /* move wargs quest from 'Stronghold' to 'Angband' */
    if (d && no_wilderness)
        d = DUNGEON_ANGBAND;
    return d;
}

static bool _find_quest_p(quest_ptr q) { return q->dungeon && q->status < QS_COMPLETED; }
static quest_ptr _find_quest(int dungeon, int level)
{
    int     i;
    vec_ptr v = _quests_get(_find_quest_p);
    quest_ptr result = NULL;

    for (i = 0; i < vec_length(v); i++)
    {
        quest_ptr q = vec_get(v, i);
        int       d = _quest_dungeon(q);

        if (d == dungeon && q->level == level)
        {
            result = q;
            break;
        }
    }
    vec_free(v);
    return result;
}

void quests_on_generate(int dungeon, int level)
{
    quest_ptr q = _find_quest(dungeon, level);
    if (q)
    {
        _current = q->id;
        q->status = QS_IN_PROGRESS;
    }
}

void quests_generate(int id)
{
    quest_ptr q = quests_get(id);
    _current = id;
    q->status = QS_IN_PROGRESS;
    quest_generate(q);
}

void quests_on_restore_floor(int dungeon, int level)
{
    quest_ptr q = _find_quest(dungeon, level);
    if (q)
    {
        _current = q->id;
        q->status = QS_IN_PROGRESS;
        quest_post_generate(q); /* replace quest monsters */
    }
}

void quests_on_kill_mon(mon_ptr mon)
{
    quest_ptr q;
    if (!_current) return;
    q = quests_get(_current);
    assert(q);
    assert(mon);
    if (q->goal == QG_KILL_MON && mon->r_idx == q->goal_idx)
    {
        q->goal_current++;
        if (q->goal_current >= q->goal_count)
            quest_complete(q, point(mon->fx, mon->fy));
    }
    else if (q->goal == QG_CLEAR_LEVEL)
    {
        int i;
        bool done = TRUE;
        if (!is_hostile(mon)) return;
        for (i = 1; i < max_m_idx && done; i++)
        {
            mon_ptr m = &m_list[i];
            if (!m->r_idx) continue;
            if (m == mon) continue;
            if (is_hostile(m)) done = FALSE;
        }
        if (done)
            quest_complete(q, point(mon->fx, mon->fy));
    }
}

void quests_on_get_obj(obj_ptr obj)
{
    quest_ptr q;
    if (!_current) return;
    q = quests_get(_current);
    assert(q);
    assert(obj);
    if (q->goal == QG_FIND_ART && (obj->name1 == q->goal_idx || obj->name3 == q->goal_idx))
        quest_complete(q, point(px, py));
}

bool quests_check_leave(void)
{
    quest_ptr q;
    if (!_current) return TRUE;
    q = quests_get(_current);
    assert(q);
    if (q->status == QS_IN_PROGRESS)
    {
        if (q->flags & QF_RETAKE)
        {
            char       c;
            string_ptr s = string_alloc();

            string_append_s(s, "<color:r>Warning,</color> you are about to leave the quest: <color:R>");
            if ((q->flags & QF_RANDOM) && q->goal == QG_KILL_MON)
                string_printf(s, "Kill %s", r_name + r_info[q->goal_idx].name);
            else
                string_append_s(s, q->name);
            string_append_s(s, "</color>. You may return to this quest later though. "
                               "Are you sure you want to leave? <color:y>[Y,n]</color>");
            c = msg_prompt(string_buffer(s), "ny", PROMPT_YES_NO);
            string_free(s);
            if (c == 'n') return FALSE;
        }
        else
        {
            char       c;
            string_ptr s = string_alloc();

            string_append_s(s, "<color:r>Warning,</color> you are about to leave the quest: <color:R>");
            if ((q->flags & QF_RANDOM) && q->goal == QG_KILL_MON)
                string_printf(s, "Kill %s", r_name + r_info[q->goal_idx].name);
            else
                string_append_s(s, q->name);
            string_append_s(s, "</color>. <color:v>You will fail this quest if you leave!</color> "
                               "Are you sure you want to leave? <color:y>[Y,n]</color>");
            c = msg_prompt(string_buffer(s), "nY", PROMPT_NEW_LINE | PROMPT_ESCAPE_DEFAULT | PROMPT_CASE_SENSITIVE);
            string_free(s);
            if (c == 'n') return FALSE;
        }
    }
    return TRUE;
}

void quests_on_leave(void)
{
    quest_ptr q;
    if (!_current) return;
    q = quests_get(_current);
    assert(q);
    leaving_quest = _current; /* XXX */
    if (q->status == QS_IN_PROGRESS)
    {
        if (q->flags & QF_RETAKE)
            q->status = QS_TAKEN;
        else
        {
            quest_fail(q);
            if (q->goal == QG_KILL_MON)
                r_info[q->goal_idx].flags1 &= ~RF1_QUESTOR;
        }
    }
    prepare_change_floor_mode(CFM_NO_RETURN);
    /* Hack: Return to surface */
    if ((q->flags & QF_GENERATE) && !q->dungeon)
        dun_level = 0;
    _current = 0;
}

bool quests_allow_downstairs(void)
{
    return !_current;
}

bool quests_allow_downshaft(void)
{
    quest_ptr q;
    if (_current) return FALSE;
    q = _find_quest(dungeon_type, dun_level + 1);
    if (q) return FALSE;
    return TRUE;
}

bool quests_allow_all_spells(void)
{
    quest_ptr q;
    if (!_current) return TRUE;
    q = quests_get(_current);
    assert(q);
    return !(q->flags & QF_GENERATE);
}

bool quests_allow_feeling(void)
{
    quest_ptr q;
    if (!_current) return TRUE;
    q = quests_get(_current);
    assert(q);
    return !(q->flags & QF_GENERATE);
}

/************************************************************************
 * Quests: Display
 ***********************************************************************/
void quests_display(void)
{
}

static void quest_doc(quest_ptr q, doc_ptr doc)
{
    if (q->flags & QF_RANDOM)
    {
        if (q->goal == QG_KILL_MON)
        {
            if (q->completed_lev == 0)
            {
                doc_printf(doc, "  %-40s (Dungeon level: %3d) - (Cancelled)\n",
                    r_name + r_info[q->goal_idx].name,
                    q->level);
            }
            else
            {
                doc_printf(doc, "  %-40s (Dungeon level: %3d) - level %2d\n",
                    r_name + r_info[q->goal_idx].name,
                    q->level,
                    q->completed_lev);
            }
        }
    }
    else
    {
        doc_printf(doc, "  %-40s (Danger  level: %3d) - level %2d\n",
            q->name, q->level, q->completed_lev);
    }
}

void quests_doc(doc_ptr doc)
{
    int     i;
    vec_ptr v = quests_get_finished();

    if (vec_length(v))
    {
        doc_printf(doc, "  <color:G>Completed Quests</color>\n");
        for (i = 0; i < vec_length(v); i++)
        {
            quest_ptr quest = vec_get(v, i);
            quest_doc(quest, doc);
        }
    }
    vec_free(v);
    doc_newline(doc);

    v = quests_get_failed();
    if (vec_length(v))
    {
        doc_printf(doc, "  <color:r>Failed Quests</color>\n");
        for (i = 0; i < vec_length(v); i++)
        {
            quest_ptr quest = vec_get(v, i);
            quest_doc(quest, doc);
        }
    }
    vec_free(v);
    doc_newline(doc);
}

/************************************************************************
 * Quests: Savefiles
 ***********************************************************************/
void quests_load(savefile_ptr file)
{
    int ct = savefile_read_s16b(file);
    int i;
    for (i = 0; i < ct; i++)
    {
        int       id = savefile_read_s16b(file);
        quest_ptr q = quests_get(id);

        assert(q);
        q->status = savefile_read_byte(file);
        q->completed_lev = savefile_read_byte(file);
        q->goal_current = savefile_read_s16b(file);
        if (q->flags & QF_RANDOM)
        {
            q->level = savefile_read_s16b(file);
            q->goal_idx  = savefile_read_s32b(file);
            q->goal_count = savefile_read_s16b(file);
            q->seed  = savefile_read_u32b(file);
        }

        if (q->goal == QG_FIND_ART)
            a_info[q->goal_idx].gen_flags |= OFG_QUESTITEM;
        if (q->goal == QG_KILL_MON && !p_ptr->is_dead && q->status < QS_COMPLETED)
        {
            monster_race *r_ptr = &r_info[q->goal_idx];
            if (r_ptr->flags1 & RF1_UNIQUE)
                r_ptr->flags1 |= RF1_QUESTOR;
        }
    }
    _current = savefile_read_s16b(file);
}

void quests_save(savefile_ptr file)
{
    int i;
    vec_ptr v = quests_get_all();
    savefile_write_s16b(file, vec_length(v));
    for (i = 0; i < vec_length(v); i++)
    {
        quest_ptr q = vec_get(v, i);
        savefile_write_s16b(file, q->id);
        savefile_write_byte(file, q->status);
        savefile_write_byte(file, q->completed_lev);
        savefile_write_s16b(file, q->goal_current);
        if (q->flags & QF_RANDOM)
        {
            savefile_write_s16b(file, q->level); /* in case I randomize later ... */
            savefile_write_s32b(file, q->goal_idx);
            savefile_write_s16b(file, q->goal_count);
            savefile_write_u32b(file, q->seed);
        }
    }
    savefile_write_s16b(file, _current);
    vec_free(v);
}

/************************************************************************
 * Quests: Wizard Utilities (^Aq)
 * This is a lot of work, but it really helps me test things.
 ***********************************************************************/
struct _ui_context_s
{
    vec_ptr quests;
    int     top;
    int     page_size;
    doc_ptr doc;
};
typedef struct _ui_context_s _ui_context_t, *_ui_context_ptr;

static void _display_menu(_ui_context_ptr context);
static void _map_cmd(_ui_context_ptr context);
static void _status_cmd(_ui_context_ptr context, int status);
static void _reward_cmd(_ui_context_ptr context);
static void _analyze_cmd(_ui_context_ptr context);
static void _display_map(room_ptr room);

void quests_wizard(void)
{
    _ui_context_t context = {0};
    context.quests = quests_get_all();

    forget_lite(); /* resizing the term would redraw the map ... sigh */
    forget_view();
    character_icky = TRUE;

    msg_line_clear();
    msg_line_init(ui_shop_msg_rect());

    Term_clear();
    context.doc = doc_alloc(MIN(80, ui_shop_rect().cx));
    for (;;)
    {
        int    max = vec_length(context.quests) - 1;
        rect_t r = ui_shop_rect(); /* recalculate in case resize */
        int    cmd;

        context.page_size = MIN(26, r.cy - 7);
        if (context.top % context.page_size != 0) /* resize?? */
            context.top = 0;

        _display_menu(&context);

        cmd = inkey_special(TRUE);
        msg_line_clear();
        msg_boundary(); /* turn_count is unchanging while in home/museum */
        if (cmd == ESCAPE || cmd == 'q' || cmd == 'Q') break;
        switch (cmd) /* cmd is from the player's perspective */
        {
        case 'm': _map_cmd(&context); break;
        case 'R': quests_on_birth(); break; /* wizards need no confirmation prompt! */
        case 'c': _status_cmd(&context, QS_COMPLETED); break;
        case 'f': _status_cmd(&context, QS_FAILED); break;
        case 'u': _status_cmd(&context, QS_UNTAKEN); break;
        case '$': _reward_cmd(&context); break;
        case '?': _analyze_cmd(&context); break;
        case KTRL('P'): do_cmd_messages(game_turn); break;
        case SKEY_PGDOWN: case '3':
            if (context.top + context.page_size - 1 < max)
                context.top += context.page_size;
            break;
        case SKEY_PGUP: case '9':
            if (context.top >= context.page_size)
                context.top -= context.page_size;
            break;
        default:
            if (cmd < 256 && isprint(cmd))
            {
                msg_format("Unrecognized command: <color:R>%c</color>. "
                           "Press <color:keypress>?</color> for help.", cmd);
            }
            else if (KTRL('A') <= cmd && cmd <= KTRL('Z'))
            {
                cmd |= 0x40;
                msg_format("Unrecognized command: <color:R>^%c</color>. "
                           "Press <color:keypress>?</color> for help.", cmd);
            }
        }
    }
    character_icky = FALSE;
    msg_line_clear();
    msg_line_init(ui_msg_rect());

    Term_clear();
    do_cmd_redraw();

    vec_free(context.quests);
    doc_free(context.doc);
}

static cptr _status_name(int status)
{
    switch (status)
    {
    case QS_UNTAKEN: return "Untaken";
    case QS_TAKEN: return "Taken";
    case QS_IN_PROGRESS: return "In Progress";
    case QS_COMPLETED: return "Completed";
    case QS_FINISHED: return "Finished";
    case QS_FAILED: return "Failed";
    case QS_FAILED_DONE: return "FailedDone";
    }
    return "";
}
static char _status_color(int status)
{
    switch (status)
    {
    case QS_UNTAKEN: return 'D';
    case QS_IN_PROGRESS: return 'y';
    case QS_COMPLETED: case QS_FINISHED: return 'G';
    case QS_FAILED: case QS_FAILED_DONE: return 'r';
    }
    return 'w';
}
static char _quest_color(quest_ptr q)
{
    if (q->status < QS_IN_PROGRESS && (q->flags & QF_RANDOM) && q->goal == QG_KILL_MON)
        return 'B';
    return _status_color(q->status);
}
static void _display_menu(_ui_context_ptr context)
{
    rect_t   r = ui_shop_rect();
    doc_ptr  doc = context->doc;
    int      i;

    doc_clear(doc);
    doc_insert(doc, "<style:table>");

    doc_printf(doc, "   <color:U>%-40.40s %-10.10s Lvl</color>\n", "Quest", "Status");
    for (i = 0; i < context->page_size; i++)
    {
        int idx = context->top + i;
        if (idx < vec_length(context->quests))
        {
            quest_ptr quest = vec_get(context->quests, idx);
            doc_printf(doc, "%c) <color:%c>", I2A(i), _quest_color(quest));
            if ((quest->flags & QF_RANDOM) && quest->goal == QG_KILL_MON)
            {
                if (quest->goal_count > 1)
                {
                    string_ptr s = string_alloc_format("%s (%d)", r_name + r_info[quest->goal_idx].name, quest->goal_count);
                    doc_printf(doc, "%-40.40s ", string_buffer(s));
                    string_free(s);
                }
                else
                    doc_printf(doc, "%-40.40s ", r_name + r_info[quest->goal_idx].name);
            }
            else
                doc_printf(doc, "%-40.40s ", quest->name);
            doc_printf(doc, "%-10.10s %3d</color>", _status_name(quest->status), quest->level);
            doc_newline(doc);
        }
        else
            doc_newline(doc);
    }
    doc_newline(doc);
    {
        int max = vec_length(context->quests) - 1;
        int bottom = context->top + context->page_size;

        if (context->top > 0 || bottom < max)
        {
            int page_count = (max - 1) / context->page_size + 1;
            int page_current = context->top / context->page_size + 1;

            doc_printf(doc, "<color:B>(Page %d of %d)</color>\n", page_current, page_count);
        }
    }

    doc_insert(doc,
        "<color:keypress>m</color> to display map. "
        "<color:keypress>R</color> to reset all quests and re-assign all random quests.\n"
        "<color:keypress>$</color> to see reward. "
        "<color:keypress>c</color> to set complete. "
        "<color:keypress>f</color> to set failed. "
        "<color:keypress>u</color> to set untaken.\n"
        "<color:keypress>?</color> to analyze quest file.\n");

    doc_insert(doc, "<color:keypress>Esc</color> to exit.</style>");

    Term_clear_rect(r);
    doc_sync_term(doc,
        doc_range_top_lines(doc, r.cy),
        doc_pos_create(r.x, r.y));
}

static void _map_cmd(_ui_context_ptr context)
{
    for (;;)
    {
        char cmd;
        int  idx;

        if (!msg_command("<color:y>View which quest map <color:w>(<color:keypress>Esc</color> to cancel)</color>?</color>", &cmd)) break;
        if (cmd == ESCAPE) break;
        if (cmd < 'a' || cmd > 'z') continue;
        idx = A2I(cmd);
        idx += context->top;
        if (idx < vec_length(context->quests))
        {
            quest_ptr quest = vec_get(context->quests, idx);
            room_ptr  map;
            if (!(quest->flags & QF_GENERATE) || !quest->file)
            {
                msg_format("The <color:R>%s</color> quest has no map.", quest->name);
                continue;
            }
            map = quest_get_map(quest);
            if (!map)
            {
                msg_format("Unable to load the <color:R>%s</color> quest map.", quest->name);
                continue;
            }
            _display_map(map);
            Term_clear();
            room_free(map);
            break;
        }
    }
}
static void _display_map(room_ptr room)
{
    int which = 0, x, y, cmd;
    int animate = 0;
    bool random = TRUE;

    msg_line_clear();
    for (;;)
    {
        transform_ptr xform;

        if (random)
            xform = transform_alloc_room(room, size(MAX_WID, MAX_HGT));
        else
            xform = transform_alloc(which, rect(0, 0, room->width, room->height));

        if (xform->dest.cx < Term->wid)
            xform->dest = rect_translate(xform->dest, (Term->wid - xform->dest.cx)/2, 0);

        if (xform->dest.cy < Term->hgt)
            xform->dest = rect_translate(xform->dest, 0, (Term->hgt - xform->dest.cy)/2);

        Term_clear();
        for (y = 0; y < room->height; y++)
        {
            cptr line = vec_get(room->map, y);
            for (x = 0; x < room->width; x++)
            {
                char letter = line[x];
                point_t p = transform_point(xform, point(x,y));
                if (0 <= p.x && p.x < Term->wid && 0 <= p.y && p.y < Term->hgt)
                {
                    room_grid_ptr grid = int_map_find(room->letters, letter);
                    int           r_idx = 0, k_idx = 0;
                    byte          a = TERM_WHITE;
                    char          c = letter;

                    if (grid && grid->scramble)
                        grid = int_map_find(room->letters, grid->scramble);

                    if (grid && grid->monster)
                    {
                        if (!(grid->flags & (ROOM_GRID_MON_CHAR |
                                ROOM_GRID_MON_RANDOM | ROOM_GRID_MON_TYPE)))
                        {
                            r_idx = grid->monster;
                        }
                    }
                    if (grid && grid->object)
                    {
                        if (!(grid->flags & (ROOM_GRID_OBJ_TYPE |
                                ROOM_GRID_OBJ_ARTIFACT | ROOM_GRID_OBJ_RANDOM)))
                        {
                            k_idx = grid->object;
                        }
                    }
                    if (r_idx)
                    {
                        monster_race *r_ptr = &r_info[r_idx];
                        a = r_ptr->x_attr;
                        c = r_ptr->x_char;
                    }
                    else if (k_idx)
                    {
                        object_kind *k_ptr = &k_info[k_idx];
                        a = k_ptr->x_attr;
                        c = k_ptr->x_char;
                    }
                    else if (grid)
                    {
                        feature_type *f_ptr = &f_info[grid->cave_feat ? grid->cave_feat : feat_floor];
                        if (f_ptr->mimic) f_ptr = &f_info[f_ptr->mimic];
                        a = f_ptr->x_attr[F_LIT_STANDARD];
                        c = f_ptr->x_char[F_LIT_STANDARD];
                    }
                    Term_putch(p.x, p.y, a, c);
                }
            }
            if (animate)
            {
                Term_fresh();
                Term_xtra(TERM_XTRA_DELAY, animate);
            }
        }
        transform_free(xform);
        cmd = inkey_special(FALSE);
        if (cmd == ESCAPE || cmd == 'q' || cmd == 'Q' || cmd == '\r') break;
        if ('0' <= cmd && cmd < '8') which = cmd - '0';
    }
    do_cmd_redraw();
}
static void _status_cmd(_ui_context_ptr context, int status)
{
    for (;;)
    {
        char cmd;
        int  idx;

        if (!msg_command(
            format("<color:y>Change status to <color:B>%s</color> for which quest <color:w>(<color:keypress>Esc</color> to cancel)</color>?</color>",
                _status_name(status)), &cmd)) break;
        if (cmd == ESCAPE) break;
        if (cmd < 'a' || cmd > 'z') continue;
        idx = A2I(cmd);
        idx += context->top;
        if (idx < vec_length(context->quests))
        {
            quest_ptr quest = vec_get(context->quests, idx);
            quest->status = status;
            if (quest->status >= QS_COMPLETED)
                quest->completed_lev = p_ptr->lev;
            break;
        }
    }
}
static void _reward_cmd(_ui_context_ptr context)
{
    for (;;)
    {
        char cmd;
        int  idx;

        if (!msg_command("<color:y>View which quest reward <color:w>(<color:keypress>Esc</color> when done)</color>?</color>", &cmd)) break;
        if (cmd == ESCAPE) break;
        if (cmd < 'a' || cmd > 'z') continue;
        idx = A2I(cmd);
        idx += context->top;
        if (idx < vec_length(context->quests))
        {
            quest_ptr quest = vec_get(context->quests, idx);
            obj_ptr   reward;

            quest->seed = randint0(0x10000000);
            reward = quest_get_reward(quest);

            if (!reward)
                msg_format("<color:R>%s</color> has no reward.", quest->name);
            else
            {
                char name[MAX_NLEN];
                obj_identify_fully(reward);
                object_desc(name, reward, OD_COLOR_CODED);
                msg_format("<color:R>%s</color> gives %s.", quest->name, name);
                if (reward->name1) a_info[reward->name1].generated = FALSE;
                obj_free(reward);
            }
        }
    }
}
static errr _parse_debug(char *line, int options)
{
    if (line[0] == 'R')
        return _parse_reward(line, options);
    return _parse_room(line, options);
}
static void _analyze_cmd(_ui_context_ptr context)
{
    for (;;)
    {
        char cmd;
        int  idx;

        if (!msg_command("<color:y>Analyze which quest file <color:w>(<color:keypress>Esc</color> to cancel)</color>?</color>", &cmd)) break;
        if (cmd == ESCAPE) break;
        if (cmd < 'a' || cmd > 'z') continue;
        idx = A2I(cmd);
        idx += context->top;
        if (idx < vec_length(context->quests))
        {
            quest_ptr quest = vec_get(context->quests, idx);
            if (!quest->file || !strlen(quest->file))
            {
                msg_format("<color:R>%s</color> has no quest file.", quest->name);
                continue;
            }
            /* very hackish ... but very useful */
            _temp_room = room_alloc(quest->name);
            _temp_reward = malloc(sizeof(room_grid_t));
            memset(_temp_reward, 0, sizeof(room_grid_t));
            trace_doc = context->doc;
            doc_clear(context->doc);
            parse_edit_file(quest->file, _parse_debug, INIT_DEBUG);
            room_free(_temp_room);
            free(_temp_reward);
            _temp_room = NULL;
            _temp_reward = NULL;
            trace_doc = NULL;
            Term_clear();
            doc_display(context->doc, quest->name, 0);
            Term_clear();
            break;
        }
    }
}
