/*
 *  File:       monstuff.cc
 *  Summary:    Misc monster related functions.
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 *
 *  Change History (most recent first):
 *
 *      <8>      7 Aug  2001   MV      Inteligent monsters now pick up gold
 *      <7>     26 Mar  2001   GDL     Fixed monster reaching
 *      <6>     13 Mar  2001   GDL     Rewrite of monster AI
 *      <5>     31 July 2000   GDL     More Manticore fixes.
 *      <4>     29 July 2000   JDJ     Fixed a bunch of places in handle_pickup where MSLOT_WEAPON
 *                                     was being erroneously used.
 *      <3>     25 July 2000   GDL     Fixed Manticores
 *      <2>     11/23/99       LRH     Upgraded monster AI
 *      <1>     -/--/--        LRH     Created
 */

#include "AppHdr.h"
#include "monstuff.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef DOS
#include <conio.h>
#endif

#include "externs.h"

#include "beam.h"
#include "cloud.h"
#include "debug.h"
#include "dungeon.h"
#include "fight.h"
#include "itemname.h"
#include "items.h"
#include "itemprop.h"
#include "misc.h"
#include "monplace.h"
#include "monspeak.h"
#include "mon-pick.h"
#include "mon-util.h"
#include "mstuff2.h"
#include "notes.h"
#include "player.h"
#include "randart.h"
#include "religion.h"
#include "spl-cast.h"
#include "spells2.h"
#include "spells4.h"
#include "stuff.h"
#include "tutorial.h"
#include "view.h"
#include "stash.h"

static bool handle_special_ability(struct monsters *monster, bolt & beem);
static bool handle_pickup(struct monsters *monster);
static void handle_behaviour(struct monsters *monster);
static void mons_in_cloud(struct monsters *monster);
static void monster_move(struct monsters *monster);
static bool plant_spit(struct monsters *monster, struct bolt &pbolt);
static int map_wand_to_mspell(int wand_type);

// [dshaligram] Doesn't need to be extern.
static int mmov_x, mmov_y;

static int compass_x[8] = { -1, 0, 1, 1, 1, 0, -1, -1 };
static int compass_y[8] = { -1, -1, -1, 0, 1, 1, 1, 0 };

static bool immobile_monster[MAX_MONSTERS];

#define FAR_AWAY    1000000         // used in monster_move()

// This function creates an artificial item to represent a mimic's appearance.
// Eventually, mimics could be redone to be more like Dancing wepaons...
// there'd only be one type and it would look like the item it carries. -- bwr
void get_mimic_item( const monsters *mimic, item_def &item )
{
    ASSERT( mimic != NULL && mons_is_mimic( mimic->type ) );

    item.base_type = OBJ_UNASSIGNED;
    item.sub_type = 0;
    item.special = 0;
    item.colour = 0;
    item.flags = 0;
    item.quantity = 1;
    item.plus = 0;
    item.plus2 = 0;
    item.x = mimic->x;
    item.y = mimic->y;
    item.link = NON_ITEM;

    int prop = 127 * mimic->x + 269 * mimic->y;

    switch (mimic->type)
    {
    case MONS_WEAPON_MIMIC:
        item.base_type = OBJ_WEAPONS;
        item.sub_type = (59 * mimic->x + 79 * mimic->y) % NUM_WEAPONS;

        prop %= 100;

        if (prop < 20)
        {
            item.flags |= ISFLAG_RANDART;
            item.special = ((mimic->x << 8 + mimic->y) & RANDART_SEED_MASK);
        }
        else if (prop < 50)
            set_equip_desc( item, ISFLAG_GLOWING );
        else if (prop < 80)
            set_equip_desc( item, ISFLAG_RUNED );
        else if (prop < 85)
            set_equip_race( item, ISFLAG_ORCISH );
        else if (prop < 90)
            set_equip_race( item, ISFLAG_DWARVEN );
        else if (prop < 95)
            set_equip_race( item, ISFLAG_ELVEN );
        break;

    case MONS_ARMOUR_MIMIC:
        item.base_type = OBJ_ARMOUR;
        item.sub_type = (59 * mimic->x + 79 * mimic->y) % NUM_ARMOURS;
        // FIXME: Remove fudges once these armours are completely implemented.
        if (item.sub_type == ARM_STUDDED_LEATHER_ARMOUR)
            item.sub_type = ARM_CRYSTAL_PLATE_MAIL;
        else if (item.sub_type == ARM_CAP)
            item.sub_type = ARM_MOTTLED_DRAGON_ARMOUR;

        prop %= 100;

        if (prop < 20)
        {
            item.flags |= ISFLAG_RANDART;
            item.special = ((mimic->x << 8 + mimic->y) & RANDART_SEED_MASK);
        }
        else if (prop < 40)
            set_equip_desc( item, ISFLAG_GLOWING );
        else if (prop < 60)
            set_equip_desc( item, ISFLAG_RUNED );
        else if (prop < 80)
            set_equip_desc( item, ISFLAG_EMBROIDERED_SHINY );
        else if (prop < 85)
            set_equip_race( item, ISFLAG_ORCISH );
        else if (prop < 90)
            set_equip_race( item, ISFLAG_DWARVEN );
        else if (prop < 95)
            set_equip_race( item, ISFLAG_ELVEN );
        break;

    case MONS_SCROLL_MIMIC:
        item.base_type = OBJ_SCROLLS;
        item.sub_type = prop % NUM_SCROLLS;
        break;

    case MONS_POTION_MIMIC:
        item.base_type = OBJ_POTIONS;
        item.sub_type = prop % NUM_POTIONS;
        break;

    case MONS_GOLD_MIMIC:
    default:
        item.base_type = OBJ_GOLD;
        item.quantity = 5 + prop % 30;
        break;
    }

    item_colour( item ); // also sets special vals for scrolls/poitions
}

// Sets the colour of a mimic to match its description... should be called
// whenever a mimic is created or teleported. -- bwr
int get_mimic_colour( struct monsters *mimic )
{
    ASSERT( mimic != NULL && mons_is_mimic( mimic->type ) );

    if (mimic->type == MONS_SCROLL_MIMIC)
        return (LIGHTGREY);
    else if (mimic->type == MONS_GOLD_MIMIC)
        return (YELLOW);

    item_def  item;
    get_mimic_item( mimic, item );

    return (item.colour);
}

// monster curses a random player inventory item:
bool curse_an_item( char which, char power )
{
    UNUSED( power );

    /* use which later, if I want to curse weapon/gloves whatever
       which, for now: 0 = non-mummy, 1 = mummy (potions as well)
       don't change mitm.special of !odecay */

    int count = 0;
    int item  = ENDOFPACK;

    for (int i = 0; i < ENDOFPACK; i++)
    {
        if (!is_valid_item( you.inv[i] ))
            continue;

        if (you.inv[i].base_type == OBJ_WEAPONS
            || you.inv[i].base_type == OBJ_ARMOUR
            || you.inv[i].base_type == OBJ_JEWELLERY
            || you.inv[i].base_type == OBJ_POTIONS)
        {
            if (item_cursed( you.inv[i] ))
                continue;

            if (you.inv[i].base_type == OBJ_POTIONS 
                && (which != 1 || you.inv[i].sub_type == POT_DECAY))
            {
                continue;
            }

            // item is valid for cursing, so we'll give it a chance
            count++;
            if (one_chance_in( count ))
                item = i;
        }
    }

    // any item to curse?
    if (item == ENDOFPACK)
        return (false);

    // curse item:

    /* problem: changes large piles of potions */
    /* don't change you.inv_special (just for fun) */
    if (you.inv[item].base_type == OBJ_POTIONS)
    {
        you.inv[item].sub_type = POT_DECAY;
        unset_ident_flags( you.inv[item], ISFLAG_IDENT_MASK ); // all different
    }
    else
        do_curse_item( you.inv[item] );

    return (true);
}

static void monster_drop_ething(struct monsters *monster, 
                                bool mark_item_origins = false)
{
    /* drop weapons & missiles last (ie on top) so others pick up */
    int i;                  // loop variable {dlb}
    bool destroyed = false;
    bool hostile_grid = false;

    if ( grid_destroys_items(grd[monster->x][monster->y]) ) {
        hostile_grid = true;
    }

    for (i = MSLOT_GOLD; i >= MSLOT_WEAPON; i--)
    {
        int item = monster->inv[i];

        if (item != NON_ITEM)
        {
            if (hostile_grid)
            {
                destroyed = true;
                destroy_item( item );
            }
            else
            {
                move_item_to_grid( &item, monster->x, monster->y );
                if (mark_item_origins && is_valid_item(mitm[item]))
                {
                    origin_set_monster(mitm[item], monster);
                }
            }

            monster->inv[i] = NON_ITEM;
        }
    }

    if (destroyed) {
        mprf(MSGCH_SOUND,
             grid_item_destruction_message(grd[monster->x][monster->y]));
    }
}                               // end monster_drop_ething()

static void place_monster_corpse(const monsters *monster)
{
    int corpse_class = mons_species(monster->type);

    if (corpse_class == MONS_DRACONIAN)
        corpse_class = draco_subspecies(monster);

    if (monster->has_ench(ENCH_SHAPESHIFTER))
        corpse_class = MONS_SHAPESHIFTER;
    else if (monster->has_ench(ENCH_GLOWING_SHAPESHIFTER))
        corpse_class = MONS_GLOWING_SHAPESHIFTER;

    if (mons_weight(corpse_class) == 0
        || grid_destroys_items(grd[monster->x][monster->y])
        || coinflip())
    {
        return;
    }

    int o = get_item_slot();
    if (o == NON_ITEM)
        return;

    mitm[o].flags = 0;
    mitm[o].base_type = OBJ_CORPSES;
    mitm[o].plus = corpse_class;
    mitm[o].plus2 = 0;                          // butcher work done
    mitm[o].sub_type = CORPSE_BODY;
    mitm[o].special = 210;                      // rot time
    mitm[o].colour = mons_class_colour(corpse_class);
    mitm[o].quantity = 1;

    if (mitm[o].colour == BLACK)
        mitm[o].colour = monster->colour;

    // Don't care if 'o' is changed, and it shouldn't be (corpses don't stack)
    move_item_to_grid( &o, monster->x, monster->y );
    if (you.hunger_state < HS_SATIATED)
        learned_something_new(TUT_MAKE_CHUNKS);    
}                               // end place_monster_corpse()

static void tutorial_inspect_kill()
{
    if (Options.tutorial_events[TUT_KILLED_MONSTER])
        learned_something_new(TUT_KILLED_MONSTER);
    else if (Options.tutorial_left
             && (you.religion == GOD_TROG
                 || you.religion == GOD_OKAWARU
                 || you.religion == GOD_MAKHLEB)
             && !you.duration[DUR_PRAYER])
    {
        tutorial_prayer_reminder();
    }
}

void monster_die(monsters *monster, char killer, int i, bool silent)
{
    if (monster->type == -1)
        return;
    
    int xom_will_act = 0;
    int monster_killed = monster_index(monster);
    bool death_message =
        !silent && mons_near(monster) && player_monster_visible(monster);
    bool in_transit = false;
    const bool hard_reset = testbits(monster->flags, MF_HARD_RESET);

    // From time to time Trog gives you a little bonus
    if (killer == KILL_YOU && you.berserker)
    {
        if (you.religion == GOD_TROG
            && (!player_under_penance() && you.piety > random2(1000)))
        {
            int bonus = 3 + random2avg( 10, 2 );

            you.berserker += bonus;
            you.might += bonus;
            haste_player( bonus );

            mpr( "You feel the power of Trog in you as your rage grows.",
                 MSGCH_GOD, GOD_TROG );
        }
        else if (wearing_amulet( AMU_RAGE ) && one_chance_in(30))
        {
            int bonus = 2 + random2(4);

            you.berserker += bonus;
            you.might += bonus;
            haste_player( bonus );

            mpr( "Your amulet glows a violent red." );
        }
    }

    if (you.prev_targ == monster_killed)
        you.prev_targ = MHITNOT;

    const bool pet_kill =
        (MON_KILL(killer)
         && (i == ANON_FRIENDLY_MONSTER ||
             ((i >= 0 && i < 200) 
              && mons_friendly(&menv[i]))));

    if (monster->type == MONS_GIANT_SPORE
        || monster->type == MONS_BALL_LIGHTNING)
    {
        if (monster->hit_points < 1 && monster->hit_points > -15)
            return;
    }
    else if (monster->type == MONS_FIRE_VORTEX
             || monster->type == MONS_SPATIAL_VORTEX)
    {
        if (!silent)
            simple_monster_message( monster, " dissipates!",
                                    MSGCH_MONSTER_DAMAGE, MDAM_DEAD );

        if (!testbits(monster->flags, MF_CREATED_FRIENDLY))
        {
            if (YOU_KILL(killer))
                gain_exp( exper_value( monster ) );
            else if (pet_kill)
                gain_exp( exper_value( monster ) / 2 + 1 );
        }

        if (monster->type == MONS_FIRE_VORTEX)
            place_cloud(CLOUD_FIRE, monster->x, monster->y, 2 + random2(4),
                        monster->kill_alignment());
    }
    else if (monster->type == MONS_SIMULACRUM_SMALL
             || monster->type == MONS_SIMULACRUM_LARGE)
    {
        if (!silent)
            simple_monster_message(
                monster, " vaporizes!", MSGCH_MONSTER_DAMAGE,
                MDAM_DEAD );

        if (!testbits(monster->flags, MF_CREATED_FRIENDLY))
        {
            if (YOU_KILL(killer))
                gain_exp( exper_value( monster ) );
            else if (pet_kill)
                gain_exp( exper_value( monster ) / 2 + 1 );
        }

        place_cloud(CLOUD_COLD, monster->x, monster->y, 2 + random2(4),
                    monster->kill_alignment());
    }
    else if (monster->type == MONS_DANCING_WEAPON)
    {
        if (!silent)
        {
            if (hard_reset)
                simple_monster_message( monster,
                                        " disappears in a puff of smoke!" );
            else
                simple_monster_message(monster, " falls from the air.",
                                       MSGCH_MONSTER_DAMAGE, MDAM_DEAD);
        }

        if (hard_reset)
            place_cloud( CLOUD_GREY_SMOKE + random2(3),
                         monster->x, monster->y, 1 + random2(3),
                         monster->kill_alignment() );


        if (!testbits(monster->flags, MF_CREATED_FRIENDLY))
        {
            if (YOU_KILL(killer))
                gain_exp( exper_value( monster ) );
            else if (pet_kill)
                gain_exp( exper_value( monster ) / 2 + 1 );
        }
    }
    else
    {
        switch (killer)
        {
        case KILL_YOU:          /* You kill in combat. */
        case KILL_YOU_MISSILE:  /* You kill by missile or beam. */
        {
            bool created_friendly = 
                        testbits(monster->flags, MF_CREATED_FRIENDLY);

            if (!silent)
            {
                snprintf(info, INFO_SIZE,
                         "You %s %s!",
                         wounded_damaged(monster->type) ? "destroy" : "kill",
                         ptr_monam(monster, DESC_NOCAP_THE));
            }

            if (death_message)
                mpr(info, MSGCH_MONSTER_DAMAGE, MDAM_DEAD);

            if (!created_friendly)
            {
                gain_exp(exper_value( monster ));
            }
            else
            {
                if (death_message)
                    mpr("That felt strangely unrewarding.");
            }

            // killing triggers tutorial lesson
            tutorial_inspect_kill();

            // Xom doesn't care who you killed:
            if (you.religion == GOD_XOM 
                && random2(70) <= 10 + monster->hit_dice)
            {
                // postpone Xom action until after the monster dies
                xom_will_act = 1 + random2(monster->hit_dice);
            }

            // Trying to prevent summoning abuse here, so we're trying to
            // prevent summoned creatures from being done_good kills,
            // Only affects monsters friendly when created.
            if (!created_friendly)
            {
                if (you.duration[DUR_PRAYER])
                {
                    if (mons_holiness(monster) == MH_NATURAL)
                        did_god_conduct(DID_DEDICATED_KILL_LIVING, 
                                        monster->hit_dice);

                    if (mons_holiness(monster) == MH_UNDEAD)
                        did_god_conduct(DID_DEDICATED_KILL_UNDEAD, 
                                        monster->hit_dice);

                    if (mons_holiness(monster) == MH_DEMONIC)
                        did_god_conduct(DID_DEDICATED_KILL_DEMON, 
                                        monster->hit_dice);

                    //jmf: Trog hates wizards
                    if (mons_is_magic_user(monster))
                        did_god_conduct(DID_DEDICATED_KILL_WIZARD,
                                        monster->hit_dice);

                    //jmf: maybe someone hates priests?
                    if (mons_class_flag(monster->type, M_PRIEST))
                        did_god_conduct(DID_DEDICATED_KILL_PRIEST, 
                                        monster->hit_dice);
                }

                if (mons_holiness(monster) == MH_HOLY)
                    did_god_conduct(DID_KILL_ANGEL, monster->hit_dice);
            }

            // Divine health and mp restoration doesn't happen when killing
            // born-friendly monsters. The mutation still applies, however.
            if (you.mutation[MUT_DEATH_STRENGTH]
                || (!created_friendly && 
                    you.religion == GOD_MAKHLEB && you.duration[DUR_PRAYER] &&
                    (!player_under_penance() && random2(you.piety) >= 30)))
            {
                if (you.hp < you.hp_max)
                {
                    mpr("You feel a little better.");
                    inc_hp(monster->hit_dice + random2(monster->hit_dice),
                           false);
                }
            }

            if (!created_friendly 
                && (you.religion == GOD_MAKHLEB || you.religion == GOD_VEHUMET)
                && you.duration[DUR_PRAYER]
                && (!player_under_penance() && random2(you.piety) >= 30))
            {
                if (you.magic_points < you.max_magic_points)
                {
                    mpr("You feel your power returning.");
                    inc_mp( 1 + random2(monster->hit_dice / 2), false );
                }
            }

            if (you.duration[DUR_DEATH_CHANNEL]
                && mons_holiness(monster) == MH_NATURAL
                && mons_weight(mons_species(monster->type)))
            {
                if (create_monster( MONS_SPECTRAL_THING, 0, BEH_FRIENDLY,
                                    monster->x, monster->y, you.pet_target,
                                    mons_species(monster->type)) != -1)
                {
                    if (death_message)
                        mpr("A glowing mist starts to gather...");
                }
            }
            break;
        }

        case KILL_MON:          /* Monster kills in combat */
        case KILL_MON_MISSILE:  /* Monster kills by missile or beam */
            if (!silent)
                simple_monster_message(monster, " dies!", MSGCH_MONSTER_DAMAGE,
                                       MDAM_DEAD);

            // no piety loss if god gifts killed by other monsters
            if (mons_friendly(monster) && !testbits(monster->flags,MF_GOD_GIFT))
                did_god_conduct(DID_FRIEND_DIES, 1 + (monster->hit_dice / 2));

            // Trying to prevent summoning abuse here, so we're trying to
            // prevent summoned creatures from being being done_good kills.
            // Only affects creatures which were friendly when summoned.
            if (!testbits(monster->flags, MF_CREATED_FRIENDLY) && pet_kill)
            {
                bool notice = false;
                const bool anon = (i == ANON_FRIENDLY_MONSTER);
                gain_exp(exper_value( monster ) / 2 + 1);

                int targ_holy   = mons_holiness(monster),
                    attacker_holy = anon? MH_NATURAL : mons_holiness(&menv[i]);

                if (attacker_holy == MH_UNDEAD)
                {
                    if (targ_holy == MH_NATURAL)
                        notice |= 
                            did_god_conduct(DID_LIVING_KILLED_BY_UNDEAD_SLAVE,
                                        monster->hit_dice);
                }
                else if (you.religion == GOD_VEHUMET
                         || you.religion == GOD_MAKHLEB
                         || (!anon && testbits( menv[i].flags, MF_GOD_GIFT )))
                {
                    // Yes, we are splitting undead pets from the others
                    // as a way to focus Necomancy vs Summoning (ignoring
                    // Summon Wraith here)... at least we're being nice and
                    // putting the natural creature Summons together with 
                    // the Demon ones.  Note that Vehumet gets a free 
                    // pass here since those followers are assumed to
                    // come from Summoning spells...  the others are 
                    // from invocations (Zin, TSO, Makh, Kiku). -- bwr

                    if (targ_holy == MH_NATURAL)
                    {
                        notice |= did_god_conduct( DID_LIVING_KILLED_BY_SERVANT,
                                                   monster->hit_dice );

                        if (mons_class_flag( monster->type, M_EVIL ))
                        {
                            notice |= 
                                did_god_conduct( 
                                        DID_NATURAL_EVIL_KILLED_BY_SERVANT,
                                        monster->hit_dice );
                        }
                    }
                    else if (targ_holy == MH_DEMONIC)
                    {
                        notice |= did_god_conduct( DID_DEMON_KILLED_BY_SERVANT, 
                                                   monster->hit_dice );
                    }
                    else if (targ_holy == MH_UNDEAD)
                    {
                        notice |= did_god_conduct( DID_UNDEAD_KILLED_BY_SERVANT,
                                                   monster->hit_dice );
                    }
                }

                // Angel kills are always noticed.
                if (targ_holy == MH_HOLY)
                {
                    notice |= did_god_conduct( DID_ANGEL_KILLED_BY_SERVANT, 
                                               monster->hit_dice );
                }

                if (you.religion == GOD_VEHUMET 
                    && notice
                    && (!player_under_penance() && random2(you.piety) >= 30))
                {
                    /* Vehumet - only for non-undead servants (coding
                       convenience, no real reason except that Vehumet
                       prefers demons) */
                    if (you.magic_points < you.max_magic_points)
                    {
                        mpr("You feel your power returning.");
                        inc_mp( 1 + random2(monster->hit_dice / 2), false );
                    }
                }
            }
            break;

        /* Monster killed by trap/inanimate thing/itself/poison not from you */
        case KILL_MISC:
            if (!silent)
                simple_monster_message(monster, " dies!", MSGCH_MONSTER_DAMAGE,
                                       MDAM_DEAD);
            break;

        case KILL_RESET:
        /* Monster doesn't die, just goes back to wherever it came from
           This must only be called by monsters running out of time (or
           abjuration), because it uses the beam variables! Or does it??? */
            if (!silent)
                simple_monster_message( monster,
                                        " disappears in a puff of smoke!" );

            place_cloud( CLOUD_GREY_SMOKE + random2(3), monster->x,
                         monster->y, 1 + random2(3),
                         monster->kill_alignment() );

            if (monster->needs_transit())
            {
                monster->flags |= MF_BANISHED;
                monster->set_transit( level_id(LEVEL_ABYSS) );
                in_transit = true;
            }

            // fall-through

        case KILL_DISMISSED:
            monster->destroy_inventory();
            break;
        }
    }

    if (monster->type == MONS_MUMMY)
    {
        if (YOU_KILL(killer))
        {
            if (curse_an_item(1, 0))
                mpr("You feel nervous for a moment...", MSGCH_MONSTER_SPELL);
        }
    }
    else if (monster->type == MONS_GUARDIAN_MUMMY
             || monster->type == MONS_GREATER_MUMMY
             || monster->type == MONS_MUMMY_PRIEST)
    {
        if (YOU_KILL(killer))
        {
            mpr("You feel extremely nervous for a moment...",
                MSGCH_MONSTER_SPELL);

            miscast_effect( SPTYP_NECROMANCY,
                            3 + (monster->type == MONS_GREATER_MUMMY) * 8
                              + (monster->type == MONS_MUMMY_PRIEST) * 5,
                            random2avg(88, 3), 100, "a mummy death curse" );
        }
    }
    else if (monster->type == MONS_BORIS && !in_transit)
    {
        // XXX: actual blood curse effect for Boris? -- bwr

        if (one_chance_in(5))
            mons_speaks( monster );
        else
        {
            // Provide the player with an ingame clue to Boris' return.  -- bwr
            const int tmp = random2(6);
            simple_monster_message( monster, 
                    (tmp == 0) ? " says, \"You haven't seen the last of me!\"" :
                    (tmp == 1) ? " says, \"I'll get you next time!\"" :
                    (tmp == 2) ? " says, \"This isn't over yet!\"" :
                    (tmp == 3) ? " says, \"I'll be back!\"" :
                    (tmp == 4) ? " says, \"This isn't the end, it's only just beginning!\"" :
                    (tmp == 5) ? " says, \"Kill me?  I think not!\"" 
                               : " says, \"You cannot defeat me so easily!\"",
                                    MSGCH_TALK );
        }

        // Now that Boris is dead, he's a valid target for monster 
        // creation again. -- bwr
        you.unique_creatures[ monster->type ] = false;
    }

    if (killer != KILL_RESET && killer != KILL_DISMISSED)
    {
        if ( MONST_INTERESTING(monster) ||
             // XXX yucky hack
             monster->type == MONS_PLAYER_GHOST ||
             monster->type == MONS_PANDEMONIUM_DEMON )
        {
            take_note(Note(NOTE_KILL_MONSTER, monster->type, 0,
                           ptr_monam(monster, DESC_NOCAP_A, true)));
        }

        you.kills.record_kill(monster, killer, pet_kill);

        if (monster->has_ench(ENCH_ABJ))
        {
            if (mons_weight(mons_species(monster->type)))
            {
                if (monster->type == MONS_SIMULACRUM_SMALL
                    || monster->type == MONS_SIMULACRUM_LARGE)
                {
                    simple_monster_message( monster, " vaporizes!" );

                    place_cloud( CLOUD_COLD, monster->x, monster->y,
                                 1 + random2(3), monster->kill_alignment() );
                }
                else
                {
                    simple_monster_message(monster,
                                "'s corpse disappears in a puff of smoke!");

                    place_cloud( CLOUD_GREY_SMOKE + random2(3),
                                 monster->x, monster->y, 1 + random2(3),
                                 monster->kill_alignment() );
                }
            }
        }
        else
        {
            // have to add case for disintegration effect here? {dlb}
            place_monster_corpse(monster);
        }
    }

    if (!hard_reset)
        monster_drop_ething(monster, 
                            killer == KILL_YOU_MISSILE 
                            || killer == KILL_YOU
                            || pet_kill);
    monster_cleanup(monster);

    if ( xom_will_act )
        Xom_acts(true, xom_will_act, false);    
}                                                   // end monster_die

void monster_cleanup(monsters *monster)
{
    unsigned int monster_killed = monster_index(monster);
    monster->reset();

    for (int dmi = 0; dmi < MAX_MONSTERS; dmi++)
    {
        if (menv[dmi].foe == monster_killed)
            menv[dmi].foe = MHITNOT;
    }

    if (you.pet_target == monster_killed)
        you.pet_target = MHITNOT;
}                               // end monster_cleanup()

static bool jelly_divide(struct monsters * parent)
{
    int jex = 0, jey = 0;       // loop variables {dlb}
    bool foundSpot = false;     // to rid code of hideous goto {dlb}
    struct monsters *child = 0; // NULL - value determined with loop {dlb}

    if (!mons_class_flag( parent->type, M_SPLITS ) || parent->hit_points == 1)
        return (false);

    // first, find a suitable spot for the child {dlb}:
    for (jex = -1; jex < 3; jex++)
    {
        // loop moves beyond those tiles contiguous to parent {dlb}:
        if (jex > 1)
            return (false);

        for (jey = -1; jey < 2; jey++)
        {
            // 10-50 for now - must take clouds into account:
            if (mgrd[parent->x + jex][parent->y + jey] == NON_MONSTER
                && !grid_is_solid(grd[parent->x + jex][parent->y + jey])
                && (parent->x + jex != you.x_pos || parent->y + jey != you.y_pos))
            {
                foundSpot = true;
                break;
            }
        }

        if (foundSpot)
            break;
    }                           /* end of for jex */

    int k = 0;                  // must remain outside loop that follows {dlb}

    // now that we have a spot, find a monster slot {dlb}:
    for (k = 0; k < MAX_MONSTERS; k++)
    {
        child = &menv[k];

        if (child->type == -1)
            break;
        else if (k == MAX_MONSTERS - 1)
            return (false);
    }

    // handle impact of split on parent {dlb}:
    parent->max_hit_points /= 2;

    if (parent->hit_points > parent->max_hit_points)
        parent->hit_points = parent->max_hit_points;

    // create child {dlb}:
    // this is terribly partial and really requires
    // more thought as to generation ... {dlb}
    child->type = parent->type;
    child->hit_dice = parent->hit_dice;
    child->hit_points = parent->hit_points;
    child->max_hit_points = child->hit_points;
    child->ac = parent->ac;
    child->ev = parent->ev;
    child->speed = parent->speed;
    child->speed_increment = 70 + random2(5);
    child->behaviour = parent->behaviour; /* Look at this! */
    child->foe = parent->foe;
    child->attitude = parent->attitude;
    child->colour = parent->colour;
    child->enchantments = parent->enchantments;
    child->x = parent->x + jex;
    child->y = parent->y + jey;

    mgrd[child->x][child->y] = k;

    if (!simple_monster_message(parent, " splits in two!"))
    {
        if (!silenced(parent->x, parent->y) || !silenced(child->x, child->y))
            mpr("You hear a squelching noise.", MSGCH_SOUND);
    }

    return (true);
}                               // end jelly_divde()

// if you're invis and throw/zap whatever, alerts menv to your position
void alert_nearby_monsters(void)
{
    struct monsters *monster = 0;       // NULL {dlb}

    for (int it = 0; it < MAX_MONSTERS; it++)
    {
        monster = &menv[it];

        // Judging from the above comment, this function isn't
        // intended to wake up monsters, so we're only going to
        // alert monsters that aren't sleeping.  For cases where an
        // event should wake up monsters and alert them, I'd suggest 
        // calling noisy() before calling this function. -- bwr
        if (monster->type != -1 
            && monster->behaviour != BEH_SLEEP
            && mons_near(monster))
        {
            behaviour_event( monster, ME_ALERT, MHITYOU );
        }
    }
}                               // end alert_nearby_monsters()

static bool valid_morph( struct monsters *monster, int new_mclass )
{
    unsigned char current_tile = grd[monster->x][monster->y];

    // morph targets are _always_ "base" classes, not derived ones.
    new_mclass = mons_species(new_mclass);

    // [ds] Non-base draconians are much more trouble than their HD
    // suggests.
    if (mons_genus(new_mclass) == MONS_DRACONIAN
        && new_mclass != MONS_DRACONIAN
        && !player_in_branch(BRANCH_HALL_OF_ZOT)
        && !one_chance_in(10))
    {
        return (false);
    }

    /* various inappropriate polymorph targets */
    if (mons_class_holiness( new_mclass ) != mons_holiness( monster )
        || mons_class_flag( new_mclass, M_NO_EXP_GAIN )        // not helpless
        || new_mclass == mons_species( monster->type ) // must be different
        || new_mclass == MONS_PROGRAM_BUG
        || new_mclass == MONS_SHAPESHIFTER
        || new_mclass == MONS_GLOWING_SHAPESHIFTER
        // These shouldn't happen anyways (demons unaffected + holiness check), 
        // but if we ever do have polydemon, these will be needed:
        || new_mclass == MONS_PLAYER_GHOST
        || new_mclass == MONS_PANDEMONIUM_DEMON
        || new_mclass == MONS_ROYAL_JELLY
        || new_mclass == MONS_ORANGE_STATUE
        || new_mclass == MONS_SILVER_STATUE
        || (new_mclass >= MONS_GERYON && new_mclass <= MONS_ERESHKIGAL))
    {
        return (false);
    }

    // Determine if the monster is happy on current tile
    return (monster_habitable_grid(new_mclass, current_tile));
}        // end valid_morph()

// note that power is (as of yet) unused within this function -
// may be worthy of consideration of later implementation, though,
// so I'll still let the parameter exist for the time being {dlb}
bool monster_polymorph( struct monsters *monster, int targetc, int power )
{
    char str_polymon[INFO_SIZE] = "";      // cannot use info[] here {dlb}
    bool player_messaged = false;
    int source_power, target_power, relax;
    int tries = 1000;

    UNUSED( power );

    // Used to be mons_power, but that just returns hit_dice 
    // for the monster class.  By using the current hit dice 
    // the player gets the opportunity to use draining more 
    // effectively against shapeshifters. -- bwr
    source_power = monster->hit_dice;
    relax = 2;

    if (targetc == RANDOM_MONSTER)
    {
        do
        {
            // Pick a monster that's guaranteed happy at this grid
            targetc = random_monster_at_grid(monster->x, monster->y);

            // valid targets are always base classes ([ds] which is unfortunate
            // in that well-populated monster classes will dominate polymorphs)
            targetc = mons_species( targetc );

            target_power = mons_power( targetc );

            if (one_chance_in(100))
                relax++;

            if (relax > 50)
                return (simple_monster_message( monster, " shudders." ));
        }
        while (tries-- && (!valid_morph( monster, targetc )
                || target_power < source_power - relax
                || target_power > source_power + (relax * 3) / 2));
    }

    if(!valid_morph( monster, targetc )) {
        strcat( str_polymon, " looks momentarily different.");
        player_messaged = simple_monster_message( monster, str_polymon );
        return (player_messaged);
    }

    // If old monster is visible to the player, and is interesting,
    // then note why the interesting monster went away.
    if (player_monster_visible(monster) && mons_near(monster)
            && MONST_INTERESTING(monster))
    {
        take_note(Note(NOTE_POLY_MONSTER, monster->type, 0,
                    ptr_monam(monster, DESC_NOCAP_A, true)));
    }

    // messaging: {dlb}
    bool invis = (mons_class_flag( targetc, M_INVIS ) 
                  || monster->has_ench(ENCH_INVIS)) &&
        (!player_see_invis());

    if (monster->has_ench(ENCH_GLOWING_SHAPESHIFTER, ENCH_SHAPESHIFTER))
        strcat( str_polymon, " changes into " );
    else if (targetc == MONS_PULSATING_LUMP)
        strcat( str_polymon, " degenerates into " );
    else
        strcat( str_polymon, " evaporates and reforms as " );

    if (invis)
        strcat( str_polymon, "something you cannot see!" );
    else
    {
        strcat( str_polymon, monam( NULL, 250, targetc, true, DESC_NOCAP_A ) );

        if (targetc == MONS_PULSATING_LUMP)
            strcat( str_polymon, " of flesh" );

        strcat( str_polymon, "!" );
    }

    player_messaged = simple_monster_message( monster, str_polymon );

    // the actual polymorphing:
    int old_hp = monster->hit_points;
    int old_hp_max = monster->max_hit_points;

    /* deal with mons_sec */
    monster->type = targetc;
    monster->number = 250;

    mon_enchant abj = monster->get_ench(ENCH_ABJ);
    mon_enchant shifter = monster->get_ench(ENCH_GLOWING_SHAPESHIFTER,
                                            ENCH_SHAPESHIFTER);

    // Note: define_monster() will clear out all enchantments! -- bwr
    define_monster( monster_index(monster) );

    monster->add_ench(abj);
    monster->add_ench(shifter);

    if (mons_class_flag( monster->type, M_INVIS ))
        monster->add_ench(ENCH_INVIS);

    monster->hit_points = monster->max_hit_points
                                * ((old_hp * 100) / old_hp_max) / 100
                                + random2(monster->max_hit_points);

    if (monster->hit_points > monster->max_hit_points)
        monster->hit_points = monster->max_hit_points;

    monster->speed_increment = 67 + random2(6);

    monster_drop_ething(monster);

        // New monster type might be interesting
        mark_interesting_monst(monster);

        // If new monster is visible to player, then we've seen it
        if (player_monster_visible(monster) && mons_near(monster))
                seen_monster(monster);

    return (player_messaged);
}                                        // end monster_polymorph()

bool monster_blink(monsters *monster)
{
    int nx, ny;

    if (!random_near_space(monster->x, monster->y, nx, ny,
            false, false))
        return (false);

    mgrd[monster->x][monster->y] = NON_MONSTER;

    monster->x = nx;
    monster->y = ny;

    mgrd[nx][ny] = monster_index(monster);

    if (player_monster_visible(monster) && mons_near(monster))
        seen_monster(monster);

    return (true);
}                               // end monster_blink()

// allow_adjacent:  allow target to be adjacent to origin
// restrict_LOS:    restict target to be within PLAYER line of sight
bool random_near_space(int ox, int oy, int &tx, int &ty, bool allow_adjacent,
    bool restrict_LOS)
{
    int tries = 0;

    do
    {
        tx = ox - 6 + random2(14);
        ty = oy - 6 + random2(14);

        // origin is not 'near'
        if (tx == ox && ty == oy)
            continue;

        tries++;

        if (tries > 149)
            break;
    }
    while ((!see_grid(tx, ty) && restrict_LOS)
           || grd[tx][ty] < DNGN_SHALLOW_WATER
           || mgrd[tx][ty] != NON_MONSTER
           || (tx == you.x_pos && ty == you.y_pos)
           || (!allow_adjacent && distance(ox, oy, tx, ty) <= 2));

    return (tries < 150);
}                               // end random_near_space()

static bool habitat_okay( struct monsters *monster, int targ )
{
    return (monster_habitable_grid(monster, targ));
}

// This doesn't really swap places, it just sets the monster's
// position equal to the player (the player has to be moved afterwards).
// It also has a slight problem with the fact the if the player is
// levitating over an inhospitable habitat for the monster the monster
// will be put in a place it normally couldn't go (this could be a
// feature because it prevents insta-killing).  In order to prevent
// that little problem, we go looking for a square for the monster
// to "scatter" to instead... and if we can't find one the monster
// just refuses to be swapped (not a bug, this is intentionally
// avoiding the insta-kill).  Another option is to look a bit
// wider for a vaild square (either by a last attempt blink, or
// by looking at a wider radius)...  insta-killing should be a
// last resort in this function (especially since Tome, Dig, and
// Summoning can be used to set up death traps).  If worse comes
// to worse, at least consider making the Swap spell not work
// when the player is over lava or water (if the player want's to
// swap pets to their death, we can let that go). -- bwr
bool swap_places(struct monsters *monster)
{
    int loc_x = you.x_pos;
    int loc_y = you.y_pos;

    const int mgrid = grd[monster->x][monster->y];

    const bool mon_dest_okay = habitat_okay( monster, grd[loc_x][loc_y] );
    const bool you_dest_okay =
        !is_grid_dangerous(mgrid)
        || yesno("Do you really want to step there?", false, 'n');

    if (!you_dest_okay)
        return (false);

    bool swap = mon_dest_okay;

    // chose an appropiate habitat square at random around the target.
    if (!swap)
    {
        int num_found = 0;
        int temp_x, temp_y;

        for (int x = -1; x <= 1; x++)
        {
            temp_x = you.x_pos + x;
            if (temp_x < 0 || temp_x >= GXM)
                continue;

            for (int y = -1; y <= 1; y++)
            {
                if (x == 0 && y == 0)
                    continue;

                temp_y = you.y_pos + y;
                if (temp_y < 0 || temp_y >= GYM)
                    continue;

                if (mgrd[temp_x][temp_y] == NON_MONSTER
                    && habitat_okay( monster, grd[temp_x][temp_y] ))
                {
                    // Found an appropiate space... check if we
                    // switch the current choice to this one.
                    num_found++;
                    if (one_chance_in(num_found))
                    {
                        loc_x = temp_x;
                        loc_y = temp_y;
                    }
                }
            }
        }

        if (num_found)
            swap = true;
    }

    if (swap)
    {
        mpr("You swap places.");

        mgrd[monster->x][monster->y] = NON_MONSTER;

        monster->x = loc_x;
        monster->y = loc_y;

        mgrd[monster->x][monster->y] = monster_index(monster);
    }
    else
    {
        // Might not be ideal, but it's better that insta-killing
        // the monster... maybe try for a short blinki instead? -- bwr
        simple_monster_message( monster, " resists." );
    }

    return (swap);
}                               // end swap_places()

void print_wounds(struct monsters *monster)
{
    // prevents segfault -- cannot use info[] here {dlb}
    char str_wound[INFO_SIZE];
    int  dam_level;

    if (monster->type == -1)
        return;

    if (monster->hit_points == monster->max_hit_points
        || monster->hit_points < 1)
    {
        return;
    }

    if (monster_descriptor(monster->type, MDSC_NOMSG_WOUNDS))
        return;

    strcpy(str_wound, " is ");

    if (monster->hit_points <= monster->max_hit_points / 6)
    {
        strcat(str_wound, "almost ");
        strcat(str_wound, wounded_damaged(monster->type) ? "destroyed"
                                                         : "dead");
        dam_level = MDAM_ALMOST_DEAD;
    }
    else
    {
        if (monster->hit_points <= monster->max_hit_points / 6)
        {
            strcat(str_wound, "horribly ");
            dam_level = MDAM_HORRIBLY_DAMAGED;
        }
        else if (monster->hit_points <= monster->max_hit_points / 3)
        {
            strcat(str_wound, "heavily " );
            dam_level = MDAM_HEAVILY_DAMAGED;
        }
        else if (monster->hit_points <= 3 * (monster-> max_hit_points / 4))
        {
            strcat(str_wound, "moderately ");
            dam_level = MDAM_MODERATELY_DAMAGED;
        }
        else
        {
            strcat(str_wound, "lightly ");
            dam_level = MDAM_LIGHTLY_DAMAGED;
        }

        strcat(str_wound, wounded_damaged(monster->type) ? "damaged"
                                                         : "wounded");
    }

    strcat(str_wound, ".");
    simple_monster_message(monster, str_wound, MSGCH_MONSTER_DAMAGE, dam_level);
}                               // end print_wounds()

// (true == 'damaged') [constructs, undead, etc.]
// and (false == 'wounded') [living creatures, etc.] {dlb}
bool wounded_damaged(int wound_class)
{
    // this schema needs to be abstracted into real categories {dlb}:
    const int holy = mons_class_holiness(wound_class);
    if (holy == MH_UNDEAD || holy == MH_NONLIVING || holy == MH_PLANT)
        return (true);

    return (false);
}                               // end wounded_damaged()

//---------------------------------------------------------------
//
// behaviour_event
//
// 1. Change any of: monster state, foe, and attitude
// 2. Call handle_behaviour to re-evaluate AI state and target x,y
//
//---------------------------------------------------------------
void behaviour_event( struct monsters *mon, int event, int src, 
                      int src_x, int src_y )
{
    bool isSmart = (mons_intel(mon->type) > I_ANIMAL);
    bool isFriendly = mons_friendly(mon);
    bool sourceFriendly = false;
    bool setTarget = false;
    bool breakCharm = false;

    if (src == MHITYOU)
        sourceFriendly = true;
    else if (src != MHITNOT)
        sourceFriendly = mons_friendly( &menv[src] );

    switch(event)
    {
    case ME_DISTURB:
        // assumes disturbed by noise...
        if (mon->behaviour == BEH_SLEEP)
            mon->behaviour = BEH_WANDER;

        // A bit of code to make Project Noise actually do
        // something again.  Basically, dumb monsters and 
        // monsters who aren't otherwise occupied will at 
        // least consider the (apparent) source of the noise 
        // interesting for a moment. -- bwr
        if (!isSmart || mon->foe == MHITNOT || mon->behaviour == BEH_WANDER)
        {
            mon->target_x = src_x;
            mon->target_y = src_y;
        }
        break;

    case ME_WHACK:
    case ME_ANNOY:
        // will turn monster against <src>, unless they
        // are BOTH friendly and stupid, or else fleeing anyway.
        // Hitting someone over the head, of course,
        //  always triggers this code.
        if ( event == ME_WHACK ||
             ((isFriendly != sourceFriendly || isSmart) &&
              (mon->behaviour != BEH_FLEE && mon->behaviour != BEH_PANIC)))
        {
            mon->foe = src;

            if (mon->behaviour != BEH_CORNERED)
                mon->behaviour = BEH_SEEK;

            if (src == MHITYOU)
            {
                mon->attitude = ATT_HOSTILE;
                breakCharm = true;
            }
        }

        // now set target x,y so that monster can whack
        // back (once) at an invisible foe
        if (event == ME_WHACK)
            setTarget = true;
        break;

    case ME_ALERT:
        // will alert monster to <src> and turn them
        // against them, unless they have a current foe.
        // it won't turn friends hostile either.
        if (mon->behaviour != BEH_CORNERED && mon->behaviour != BEH_PANIC &&
            mon->behaviour != BEH_FLEE)
            mon->behaviour = BEH_SEEK;

        if (mon->foe == MHITNOT)
            mon->foe = src;
        break;

    case ME_SCARE:
        mon->foe = src;
        mon->behaviour = BEH_FLEE;
        // assume monsters know where to run from, even
        // if player is invisible.
        setTarget = true;
        break;

    case ME_CORNERED:
        // just set behaviour.. foe doesn't change.
        if (mon->behaviour != BEH_CORNERED && !mon->has_ench(ENCH_FEAR))
            simple_monster_message(mon, " turns to fight!");

        mon->behaviour = BEH_CORNERED;
        break;

    case ME_EVAL:
    default:
        break;
    }

    if (setTarget)
    {
        if (src == MHITYOU)
        {
            mon->target_x = you.x_pos;
            mon->target_y = you.y_pos;
            mon->attitude = ATT_HOSTILE;
        }
        else if (src != MHITNOT)
        {
            mon->target_x = menv[src].x;
            mon->target_y = menv[src].y;
        }
    }

    // now, break charms if appropriate
    if (breakCharm)
        mon->del_ench(ENCH_CHARM);

    // do any resultant foe or state changes
    handle_behaviour( mon );
}

//---------------------------------------------------------------
//
// handle_behaviour
//
// 1. Evalutates current AI state
// 2. Sets monster targetx,y based on current foe
//
//---------------------------------------------------------------
static void handle_behaviour(struct monsters *mon)
{
    bool changed = true;
    bool isFriendly = mons_friendly(mon);
    bool proxPlayer = mons_near(mon);
    bool proxFoe;
    bool isHurt = (mon->hit_points <= mon->max_hit_points / 4 - 1);
    bool isHealthy = (mon->hit_points > mon->max_hit_points / 2);
    bool isSmart = (mons_intel(mon->type) > I_ANIMAL);
    bool isScared = mon->has_ench(ENCH_FEAR);
    bool isMobile = !mons_is_stationary(mon);

    // check for confusion -- early out.
    if (mon->has_ench(ENCH_CONFUSION))
    {
        mon->target_x = 10 + random2(GXM - 10);
        mon->target_y = 10 + random2(GYM - 10);
        return;
    }

    // validate current target exists
    if (mon->foe != MHITNOT && mon->foe != MHITYOU)
    {
        if (menv[mon->foe].type == -1)
            mon->foe = MHITNOT;
    }

    // change proxPlayer depending on invisibility and standing
    // in shallow water
    if (proxPlayer && you.invis)
    {
        if (!mons_player_visible( mon ))
            proxPlayer = false;

        // must be able to see each other
        if (!see_grid(mon->x, mon->y))
            proxPlayer = false;

        const int intel = mons_intel(mon->type);
        // now, the corollary to that is that sometimes, if a
        // player is right next to a monster, they will 'see'
        if (grid_distance( you.x_pos, you.y_pos, mon->x, mon->y ) == 1
                && one_chance_in(3))
            proxPlayer = true;

        // [dshaligram] Very smart monsters have a chance of cluing in to
        // invisible players in various ways.
        else if ((intel == I_NORMAL && one_chance_in(10))
                || (intel == I_HIGH && one_chance_in(6)))
            proxPlayer = true;
    }

    // set friendly target, if they don't already have one
    if (isFriendly 
        && you.pet_target != MHITNOT
        && (mon->foe == MHITNOT || mon->foe == MHITYOU))
    {
        mon->foe = you.pet_target;
    }

    // monsters do not attack themselves {dlb}
    if (mon->foe == monster_index(mon))
        mon->foe = MHITNOT;

    // friendly monsters do not attack other friendly monsters
    if (mon->foe != MHITNOT && mon->foe != MHITYOU)
    {
        if (isFriendly && mons_friendly(&menv[mon->foe]))
            mon->foe = MHITNOT;
    }

    // unfriendly monsters fighting other monsters will usually
    // target the player, if they're healthy
    if (!isFriendly && mon->foe != MHITYOU && mon->foe != MHITNOT
        && proxPlayer && !one_chance_in(3) && isHealthy)
    {
        mon->foe = MHITYOU;
    }

    // validate target again
    if (mon->foe != MHITNOT && mon->foe != MHITYOU)
    {
        if (menv[mon->foe].type == -1)
            mon->foe = MHITNOT;
    }

    while (changed)
    {
        int foe_x = you.x_pos;
        int foe_y = you.y_pos;

        // evaluate these each time; they may change
        if (mon->foe == MHITNOT)
            proxFoe = false;
        else
        {
            if (mon->foe == MHITYOU)
            {
                foe_x = you.x_pos;
                foe_y = you.y_pos;
                proxFoe = proxPlayer;   // take invis into account
            }
            else
            {
                proxFoe = mons_near(mon, mon->foe);

                if (!mons_monster_visible( mon, &menv[mon->foe] ))
                    proxFoe = false;

                // XXX monsters will rely on player LOS -- GDL
                if (!see_grid(menv[mon->foe].x, menv[mon->foe].y))
                    proxFoe = false;

                foe_x = menv[mon->foe].x;
                foe_y = menv[mon->foe].y;
            }
        }

        // track changes to state; attitude never changes here.
        unsigned int new_beh = mon->behaviour;
        unsigned int new_foe = mon->foe;

        // take care of monster state changes
        switch(mon->behaviour)
        {
        case BEH_SLEEP:
            // default sleep state
            mon->target_x = mon->x;
            mon->target_y = mon->y;
            new_foe = MHITNOT;
            break;

        case BEH_SEEK:
            // no foe? then wander or seek the player
            if (mon->foe == MHITNOT)
            {
                if (!proxPlayer)
                    new_beh = BEH_WANDER;
                else
                {
                    new_foe = MHITYOU;
                    mon->target_x = you.x_pos;
                    mon->target_y = you.y_pos;
                }

                break;
            }

            // foe gone out of LOS?
            if (!proxFoe)
            {
                if (isFriendly)
                {
                    new_foe = MHITYOU;
                    mon->target_x = foe_x;
                    mon->target_y = foe_y;
                    break;
                }

                if (mon->foe_memory > 0 && mon->foe != MHITNOT)
                {
                    // if we've arrived at our target x,y
                    // do a stealth check.  If the foe
                    // fails, monster will then start
                    // tracking foe's CURRENT position,
                    // but only for a few moves (smell and
                    // intuition only go so far)

                    if (mon->x == mon->target_x &&
                        mon->y == mon->target_y)
                    {
                        if (mon->foe == MHITYOU)
                        {
                            if (check_awaken(monster_index(mon)))
                            {
                                mon->target_x = you.x_pos;
                                mon->target_y = you.y_pos;
                            }
                            else
                                mon->foe_memory = 1;
                        }
                        else
                        {
                            if (coinflip())     // XXX: cheesy!
                            {
                                mon->target_x = menv[mon->foe].x;
                                mon->target_y = menv[mon->foe].y;
                            }
                            else
                                mon->foe_memory = 1;
                        }
                    }

                    // either keep chasing, or start
                    // wandering.
                    if (mon->foe_memory < 2)
                    {
                        mon->foe_memory = 0;
                        new_beh = BEH_WANDER;
                    }
                    break;
                }

                // hack: smarter monsters will
                // tend to pursue the player longer.
                int memory;
                switch(mons_intel(monster_index(mon)))
                {
                    case I_HIGH:
                        memory = 100 + random2(200);
                        break;
                    case I_NORMAL:
                        memory = 50 + random2(100);
                        break;
                    case I_ANIMAL:
                    default:
                        memory = 25 + random2(75);
                        break;
                    case I_INSECT:
                        memory = 10 + random2(50);
                        break;
                }

                mon->foe_memory = memory;
                break;      // from case
            }

            // monster can see foe: continue 'tracking'
            // by updating target x,y
            if (mon->foe == MHITYOU)
            {
                // sometimes, your friends will wander a bit.
                if (isFriendly && one_chance_in(8))
                {
                    mon->target_x = 10 + random2(GXM - 10);
                    mon->target_y = 10 + random2(GYM - 10);
                    mon->foe = MHITNOT;
                    new_beh = BEH_WANDER;
                }
                else
                {
                    mon->target_x = you.x_pos;
                    mon->target_y = you.y_pos;
                }
            }
            else
            {
                mon->target_x = menv[mon->foe].x;
                mon->target_y = menv[mon->foe].y;
            }

            if (isHurt && !isSmart && isMobile)
                new_beh = BEH_FLEE;
            break;

        case BEH_WANDER:
            // is our foe in LOS?
            // Batty monsters don't automatically reseek so that
            // they'll flitter away, we'll reset them just before 
            // they get movement in handle_monsters() instead. -- bwr
            if (proxFoe && !testbits( mon->flags, MF_BATTY ))
            {
                new_beh = BEH_SEEK;
                break;
            }

            // default wander behaviour
            //
            // XXX: This is really dumb wander behaviour... instead of
            // changing the goal square every turn, better would be to
            // have the monster store a direction and have the monster
            // head in that direction for a while, then shift the 
            // direction to the left or right.  We're changing this so 
            // wandering monsters at least appear to have some sort of 
            // attention span.  -- bwr
            if ((mon->x == mon->target_x && mon->y == mon->target_y)
                || one_chance_in(20)
                || testbits( mon->flags, MF_BATTY ))
            {
                mon->target_x = 10 + random2(GXM - 10);
                mon->target_y = 10 + random2(GYM - 10);
            }

            // during their wanderings, monsters will
            // eventually relax their guard (stupid
            // ones will do so faster, smart monsters
            // have longer memories
            if (!proxFoe && mon->foe != MHITNOT)
            {
                if (one_chance_in( isSmart ? 60 : 20 ))
                    new_foe = MHITNOT;
            }
            break;

        case BEH_FLEE:
            // check for healed
            if (isHealthy && !isScared)
                new_beh = BEH_SEEK;
            // smart monsters flee until they can
            // flee no more...  possible to get a
            // 'CORNERED' event, at which point
            // we can jump back to WANDER if the foe
            // isn't present.

            if (proxFoe)
            {
                // try to flee _from_ the correct position
                mon->target_x = foe_x;
                mon->target_y = foe_y;
            }
            break;

        case BEH_CORNERED:
            if (isHealthy)
                new_beh = BEH_SEEK;

            // foe gone out of LOS?
            if (!proxFoe)
            {
                if (isFriendly || proxPlayer)
                    new_foe = MHITYOU;
                else
                    new_beh = BEH_WANDER;
            }
            else 
            {
                mon->target_x = foe_x;
                mon->target_y = foe_y;
            }
            break;

        default:
            return;     // uh oh
        }

        changed = (new_beh != mon->behaviour || new_foe != mon->foe);
        mon->behaviour = new_beh;

        if (mon->foe != new_foe)
            mon->foe_memory = 0;

        mon->foe = new_foe;
    }
}                               // end handle_behaviour()

std::string str_simple_monster_message(monsters *mons, const char *event)
{
    if (mons_near(mons) && player_monster_visible(mons))
        return make_stringf("%s%s",
                            ptr_monam(mons, DESC_CAP_THE),
                            event );

    return ("");
}

// note that this function *completely* blocks messaging for monsters
// distant or invisible to the player ... look elsewhere for a function
// permitting output of "It" messages for the invisible {dlb}
// Intentionally avoids info and str_pass now. -- bwr
bool simple_monster_message(struct monsters *monster, const char *event,
                            int channel, int param)
{
    char buff[INFO_SIZE];

    if (mons_near( monster )
        && (channel == MSGCH_MONSTER_SPELL || player_monster_visible(monster)))
    {
        snprintf( buff, sizeof(buff), "%s%s", 
                  ptr_monam(monster, DESC_CAP_THE), event );

        mpr( buff, channel, param );
        return (true);
    }

    return (false);
}                               // end simple_monster_message()

static bool handle_enchantment(struct monsters *monster)
{
    // Yes, this is the speed we want.  This function will be called in
    // two circumstances: (1) the monster can move and has enough energy, 
    // and (2) the monster cannot move (speed == 0) and the monster loop 
    // is running.
    //
    // In the first case we don't have to figure in the player's time, 
    // since the rate of call to this function already does that (ie.
    // a bat would get here 6 times in 2 normal player turns, and if
    // the player was twice as fast it would be 6 times every four player
    // moves.  So the only speed we care about is the monster vs the
    // absolute time frame.
    //
    // In the second case, we're hacking things so that plants can suffer
    // from sticky flame.  The rate of call in this case is once every 
    // player action... so the time_taken by the player is the ratio to
    // the absolute time frame.  
    //
    // This will be used below for poison and sticky flame so that the
    // damage is apparently in the absolute time frame.  This is done
    // by scaling the damage and the chance that the effect goes away.
    // The result is that poison on a regular monster will be doing
    // 1d3 damage every two rounds, and last eight rounds, and on 
    // a bat the same poison will be doing 1/3 the damage each action
    // it gets (the mod fractions are randomized in), will have three
    // turns to the other monster's one, and the effect will survive
    // 3 times as many calls to this function (ie 8 rounds * 3 calls).
    //
    // -- bwr
    const int speed = (monster->speed == 0) ? you.time_taken : monster->speed;
    monster->apply_enchantments(speed);
    return (!monster->alive());
}                               // end handle_enchantment()

//---------------------------------------------------------------
//
// handle_movement
//
// Move the monster closer to its target square.
//
//---------------------------------------------------------------
static void handle_movement(struct monsters *monster)
{
    int dx, dy;

    // some calculations
    if (monster->type == MONS_BORING_BEETLE && monster->foe == MHITYOU)
    {
        dx = you.x_pos - monster->x;
        dy = you.y_pos - monster->y;
    }
    else
    {
        dx = monster->target_x - monster->x;
        dy = monster->target_y - monster->y;
    }

    // move the monster:
    mmov_x = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
    mmov_y = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);

    if (monster->behaviour == BEH_FLEE)
    {
        mmov_x *= -1;
        mmov_y *= -1;
    }

    // bounds check: don't let fleeing monsters try to run
    // off the map
    if (monster->target_x + mmov_x < 0 || monster->target_x + mmov_x >= GXM)
        mmov_x = 0;

    if (monster->target_y + mmov_y < 0 || monster->target_y + mmov_y >= GYM)
        mmov_y = 0;

    // now quit if we're can't move
    if (mmov_x == 0 && mmov_y == 0)
        return;

    // reproduced here is some semi-legacy code that makes monsters
    // move somewhat randomly along oblique paths.  It is an exceedingly
    // good idea, given crawl's unique line of sight properties.
    //
    // Added a check so that oblique movement paths aren't used when
    // close to the target square. -- bwr
    if (grid_distance( dx, dy, 0, 0 ) > 3)
    {
        if (abs(dx) > abs(dy))
        {
            // sometimes we'll just move parallel the x axis
            if (coinflip())
                mmov_y = 0;
        }

        if (abs(dy) > abs(dx))
        {
            // sometimes we'll just move parallel the y axis
            if (coinflip())
                mmov_x = 0;
        }
    }
}                               // end handle_movement()

//---------------------------------------------------------------
//
// handle_nearby_ability
//
// Gives monsters a chance to use a special ability when they're
// next to the player.
//
//---------------------------------------------------------------
static void handle_nearby_ability(struct monsters *monster)
{
    if (!mons_near( monster )
        || monster->behaviour == BEH_SLEEP
        || monster->has_ench(ENCH_SUBMERGED))
    {
        return;
    }

    if (mons_class_flag(monster->type, M_SPEAKS) && one_chance_in(21)
        && monster->behaviour != BEH_WANDER)
    {
        mons_speaks(monster);
    }

    switch (monster->type)
    {
    case MONS_SPATIAL_VORTEX:
    case MONS_KILLER_KLOWN:
        // used for colour (butterflies too, but they don't change)
        monster->colour = random_colour();
        break;

    case MONS_GIANT_EYEBALL:
        if (coinflip() && !mons_friendly(monster)
            && monster->behaviour != BEH_WANDER)
        {
            simple_monster_message(monster, " stares at you.");

            if (you.paralysis < 10)
                you.paralysis += 2 + random2(3);
        }
        break;

    case MONS_EYE_OF_DRAINING:
        if (coinflip() && !mons_friendly(monster)
            && monster->behaviour != BEH_WANDER)
        {
            simple_monster_message(monster, " stares at you.");

            dec_mp(5 + random2avg(13, 3));

            heal_monster(monster, 10, true); // heh heh {dlb}
        }
        break;

    case MONS_LAVA_WORM:
    case MONS_LAVA_FISH:
    case MONS_LAVA_SNAKE:
    case MONS_SALAMANDER:
    case MONS_BIG_FISH:
    case MONS_GIANT_GOLDFISH:
    case MONS_ELECTRICAL_EEL:
    case MONS_JELLYFISH:
    case MONS_WATER_ELEMENTAL:
    case MONS_SWAMP_WORM:
        // XXX: We're being a bit player-centric here right now...
        // really we should replace the grid_distance() check
        // with one that checks for unaligned monsters as well. -- bwr
        if (monster->has_ench(ENCH_SUBMERGED))
        {
            if (grd[monster->x][monster->y] == DNGN_SHALLOW_WATER
                || grd[monster->x][monster->y] == DNGN_BLUE_FOUNTAIN
                || (!mons_friendly(monster) 
                    && grid_distance( monster->x, monster->y, 
                                      you.x_pos, you.y_pos ) == 1
                    && (monster->hit_points == monster->max_hit_points
                        || (monster->hit_points > monster->max_hit_points / 2
                            && coinflip()))))
            {
                monster->del_ench(ENCH_SUBMERGED);
            }
        }
        else if (monster_can_submerge(monster->type,
                                      grd[monster->x][monster->y])
                 && (one_chance_in(5) 
                     || (grid_distance( monster->x, monster->y, 
                                        you.x_pos, you.y_pos ) > 1
                            // FIXME This is better expressed as a function
                            // such as monster_has_ranged_attack:
                            && monster->type != MONS_ELECTRICAL_EEL
                            && monster->type != MONS_LAVA_SNAKE
                            && !one_chance_in(20))
                     || monster->hit_points <= monster->max_hit_points / 2)
                     || env.cgrid[monster->x][monster->y] != EMPTY_CLOUD)
        {
            monster->add_ench(ENCH_SUBMERGED);
        }
        break;

    case MONS_AIR_ELEMENTAL:
        if (one_chance_in(5))
            monster->add_ench(ENCH_SUBMERGED);
        break;

    case MONS_PANDEMONIUM_DEMON:
        if (monster->ghost->values[ GVAL_DEMONLORD_CYCLE_COLOUR ])
            monster->colour = random_colour();
        break;
    }
}                               // end handle_nearby_ability()

//---------------------------------------------------------------
//
// handle_special_ability
//
// $$$ not sure what to say here...
//
//---------------------------------------------------------------
static bool handle_special_ability(struct monsters *monster, bolt & beem)
{
    bool used = false;

    FixedArray < unsigned int, 19, 19 > show;

    const monster_type mclass = (mons_genus( monster->type ) == MONS_DRACONIAN) 
                                  ? draco_subspecies( monster ) 
                                  : static_cast<monster_type>( monster->type );

    if (!mons_near( monster )
        || monster->behaviour == BEH_SLEEP
        || monster->has_ench(ENCH_SUBMERGED))
    {
        return (false);
    }

//    losight(show, grd, you.x_pos, you.y_pos);

    switch (mclass)
    {
    case MONS_ORANGE_STATUE:
        used = orange_statue_effects(monster);
        break;

    case MONS_SILVER_STATUE:
        used = silver_statue_effects(monster);
        break;

    case MONS_BALL_LIGHTNING:
        if (monster->attitude == ATT_HOSTILE
            && distance( you.x_pos, you.y_pos, monster->x, monster->y ) <= 5)
        {
            monster->hit_points = -1;
            used = true;
            break;
        }

        for (int i = 0; i < MAX_MONSTERS; i++)
        {
            struct monsters *targ = &menv[i];

            if (targ->type == -1 || targ->type == NON_MONSTER)
                continue;

            if (distance( monster->x, monster->y, targ->x, targ->y ) >= 5)
                continue;

            if (monster->attitude == targ->attitude)
                continue;

            // faking LOS by checking the neighbouring square
            int dx = targ->x - monster->x;
            if (dx) 
                dx /= dx;

            int dy = targ->y - monster->y;
            if (dy) 
                dy /= dy;

            const int tx = monster->x + dx;
            const int ty = monster->y + dy;

            if (tx < 0 || tx > GXM || ty < 0 || ty > GYM)
                continue;

            if (!grid_is_solid(grd[tx][ty]))
            {
                monster->hit_points = -1;
                used = true;
                break;
            }
        }
        break;

    case MONS_LAVA_SNAKE:
        if (monster->has_ench(ENCH_CONFUSION))
            break;

        if (!mons_player_visible( monster ))
            break;

        if (coinflip())
            break;

        // setup tracer
        beem.name = "glob of lava";
        beem.range = 4;
        beem.rangeMax = 13;
        beem.damage = dice_def( 3, 10 );
        beem.colour = RED;
        beem.type = SYM_ZAP;
        beem.flavour = BEAM_LAVA;
        beem.hit = 20;
        beem.beam_source = monster_index(monster);
        beem.thrower = KILL_MON;
        beem.aux_source = "glob of lava";

        // fire tracer
        fire_tracer(monster, beem);

        // good idea?
        if (mons_should_fire(beem))
        {
            simple_monster_message(monster, " spits lava!");
            fire_beam(beem);
            used = true;
        }
        break;

    case MONS_ELECTRICAL_EEL:
        if (monster->has_ench(ENCH_CONFUSION))
            break;

        if (!mons_player_visible( monster ))
            break;

        if (coinflip())
            break;

        // setup tracer
        beem.name = "bolt of electricity";
        beem.damage = dice_def( 3, 6 );
        beem.colour = LIGHTCYAN;
        beem.type = SYM_ZAP;
        beem.flavour = BEAM_ELECTRICITY;
        beem.hit = 50;
        beem.beam_source = monster_index(monster);
        beem.thrower = KILL_MON;
        beem.aux_source = "bolt of electricity";
        beem.range = 4;
        beem.rangeMax = 13;
        beem.is_beam = true;

        // fire tracer
        fire_tracer(monster, beem);

        // good idea?
        if (mons_should_fire(beem))
        {
            simple_monster_message(monster, " shoots out a bolt of electricity!");
            fire_beam(beem);
            used = true;
        }
        break;

    case MONS_ACID_BLOB:
    case MONS_OKLOB_PLANT:
    case MONS_YELLOW_DRACONIAN:
        if (monster->has_ench(ENCH_CONFUSION))
            break;

        if (!mons_player_visible( monster ))
            break;

        if (one_chance_in(3))
            used = plant_spit(monster, beem);

        break;

    case MONS_PIT_FIEND:
        if (one_chance_in(3))
            break;
        // deliberate fall through
    case MONS_FIEND:
        if (monster->has_ench(ENCH_CONFUSION))
            break;

        // friendly fiends won't use torment, preferring hellfire
        // (right now there is no way a monster can predict how
        // badly they'll damage the player with torment) -- GDL
        if (one_chance_in(4))
        {
            int spell_cast;

            switch (random2(4))
            {
            case 0:
                if (!mons_friendly(monster))
                {
                    spell_cast = MS_TORMENT;
                    mons_cast(monster, beem, spell_cast);
                    used = true;
                    break;
                }
                // deliberate fallthrough -- see above
            case 1:
            case 2:
            case 3:
                spell_cast = MS_HELLFIRE;
                setup_mons_cast(monster, beem, spell_cast);

                // fire tracer
                fire_tracer(monster, beem);

                // good idea?
                if (mons_should_fire(beem))
                {
                    simple_monster_message( monster, " makes a gesture!", 
                                            MSGCH_MONSTER_SPELL );

                    mons_cast(monster, beem, spell_cast);
                    used = true;
                }
                break;
            }

            mmov_x = 0;
            mmov_y = 0;
        }
        break;

    case MONS_IMP:
    case MONS_PHANTOM:
    case MONS_INSUBSTANTIAL_WISP:
    case MONS_BLINK_FROG:
    case MONS_KILLER_KLOWN:
        if (one_chance_in(7))
        {
            simple_monster_message(monster, " blinks.");
            monster_blink(monster);
        }
        break;

    case MONS_MANTICORE:
        if (!mons_player_visible( monster ))
            break;

        if (monster->has_ench(ENCH_CONFUSION))
            break;

        if (!mons_near(monster))
            break;

        // the fewer spikes the manticore has left, the less
        // likely it will use them.
        if (random2(16) >= static_cast<int>(monster->number))
            break;

        // do the throwing right here, since the beam is so
        // easy to set up and doesn't involve inventory.

        // set up the beam
        beem.name = "volley of spikes";
        beem.range = 9;
        beem.rangeMax = 9;
        beem.hit = 14;
        beem.damage = dice_def( 2, 10 );
        beem.beam_source = monster_index(monster);
        beem.type = SYM_MISSILE;
        beem.colour = LIGHTGREY;
        beem.flavour = BEAM_MISSILE;
        beem.thrower = KILL_MON;
        beem.aux_source = "volley of spikes";
        beem.is_beam = false;

        // fire tracer
        fire_tracer(monster, beem);

        // good idea?
        if (mons_should_fire(beem))
        {
            simple_monster_message(monster, " flicks its tail!");
            fire_beam(beem);
            used = true;
            // decrement # of volleys left
            monster->number -= 1;
        }
        break;

    // dragon breath weapon:
    case MONS_DRAGON:
    case MONS_HELL_HOUND:
    case MONS_ICE_DRAGON:
    case MONS_LINDWURM:
    case MONS_FIREDRAKE:
    case MONS_XTAHUA:
    case MONS_WHITE_DRACONIAN:
    case MONS_RED_DRACONIAN:
        if (!mons_player_visible( monster ))
            break;

        if (monster->has_ench(ENCH_CONFUSION))
            break;

        if ((monster->type != MONS_HELL_HOUND && random2(13) < 3)
            || one_chance_in(10))
        {
            setup_dragon(monster, beem);

            // fire tracer
            fire_tracer(monster, beem);

            // good idea?
            if (mons_should_fire(beem))
            {
                simple_monster_message(monster, " breathes.",
                        MSGCH_MONSTER_SPELL);
                fire_beam(beem);
                mmov_x = 0;
                mmov_y = 0;
                used = true;
            }
        }
        break;

    default:
        break;
    }

    return (used);
}                               // end handle_special_ability()

//---------------------------------------------------------------
//
// handle_potion
//
// Give the monster a chance to quaff a potion. Returns true if
// the monster imbibed.
//
//---------------------------------------------------------------
static bool handle_potion(struct monsters *monster, bolt & beem)
{

    // yes, there is a logic to this ordering {dlb}:
    if (monster->behaviour == BEH_SLEEP)
        return (false);
    else if (monster->inv[MSLOT_POTION] == NON_ITEM)
        return (false);
    else if (!one_chance_in(3))
        return (false);
    else
    {
        bool imbibed = false;

        switch (mitm[monster->inv[MSLOT_POTION]].sub_type)
        {
        case POT_HEALING:
        case POT_HEAL_WOUNDS:
            if (monster->hit_points <= monster->max_hit_points / 2
                && mons_holiness(monster) != MH_UNDEAD
                && mons_holiness(monster) != MH_NONLIVING
                && mons_holiness(monster) != MH_PLANT)
            {
                simple_monster_message(monster, " drinks a potion.");

                if (heal_monster(monster, 5 + random2(7), false))
                    simple_monster_message(monster, " is healed!");

                if (mitm[monster->inv[MSLOT_POTION]].sub_type
                                                    == POT_HEAL_WOUNDS)
                {
                    heal_monster(monster, 10 + random2avg(28, 3), false);
                }

                imbibed = true;
            }
            break;

        case POT_SPEED:
            // notice that these are the same odd colours used in
            // mons_ench_f2() {dlb}
            beem.colour = BLUE;
            // intentional fall through
        case POT_INVISIBILITY:
            if (mitm[monster->inv[MSLOT_POTION]].sub_type == POT_INVISIBILITY)
            {
                beem.colour = MAGENTA;
                // Friendly monsters won't go invisible if the player
                // can't see invisible. We're being nice.
                if ( mons_friendly(monster) && !player_see_invis(false) )
                    break;
            }

            // why only drink these if not near player? {dlb}
            if (!mons_near(monster))
            {
                simple_monster_message(monster, " drinks a potion.");

                mons_ench_f2(monster, beem);

                imbibed = true;
            }
            break;
        }

        if (imbibed)
        {
            if (dec_mitm_item_quantity( monster->inv[MSLOT_POTION], 1 ))
                monster->inv[MSLOT_POTION] = NON_ITEM;
        }

        return (imbibed);
    }
}                               // end handle_potion()

static bool handle_reaching(struct monsters *monster)
{
    bool       ret = false;
    const int  wpn = monster->inv[MSLOT_WEAPON];

    if (mons_aligned(monster_index(monster), monster->foe))
        return (false);

    if (monster->has_ench(ENCH_SUBMERGED))
        return (false);

    if (wpn != NON_ITEM && get_weapon_brand( mitm[wpn] ) == SPWPN_REACHING )
    {
        if (monster->foe == MHITYOU)
        {
            // this check isn't redundant -- player may be invisible.
            if (monster->target_x == you.x_pos && monster->target_y == you.y_pos)
            {
                int dx = abs(monster->x - you.x_pos);
                int dy = abs(monster->y - you.y_pos);

                if ((dx == 2 && dy <= 2) || (dy == 2 && dx <= 2))
                {
                    ret = true;
                    monster_attack( monster_index(monster) );
                }
            }
        }
        else if (monster->foe != MHITNOT)
        {
            // same comments as to invisibility as above.
            if (monster->target_x == menv[monster->foe].x
                && monster->target_y == menv[monster->foe].y)
            {
                int dx = abs(monster->x - menv[monster->foe].x);
                int dy = abs(monster->y - menv[monster->foe].y);
                if ((dx == 2 && dy <= 2) || (dy == 2 && dx <= 2))
                {
                    ret = true;
                    monsters_fight( monster_index(monster), monster->foe );
                }
            }
        }
    }

    return ret;
}                               // end handle_reaching()

//---------------------------------------------------------------
//
// handle_scroll
//
// Give the monster a chance to read a scroll. Returns true if
// the monster read something.
//
//---------------------------------------------------------------
static bool handle_scroll(struct monsters *monster)
{
    // yes, there is a logic to this ordering {dlb}:
    if (monster->has_ench(ENCH_CONFUSION) 
        || monster->behaviour == BEH_SLEEP
        || monster->has_ench(ENCH_SUBMERGED))
    {
        return (false);
    }
    else if (monster->inv[MSLOT_SCROLL] == NON_ITEM)
        return (false);
    else if (!one_chance_in(3))
        return (false);
    else
    {
        bool read = false;

        // notice how few cases are actually accounted for here {dlb}:
        switch (mitm[monster->inv[MSLOT_SCROLL]].sub_type)
        {
        case SCR_TELEPORTATION:
            if (!monster->has_ench(ENCH_TP))
            {
                if (monster->behaviour == BEH_FLEE)
                {
                    simple_monster_message(monster, " reads a scroll.");
                    monster_teleport(monster, false);
                    read = true;
                }
            }
            break;

        case SCR_BLINKING:
            if (monster->behaviour == BEH_FLEE)
            {
                if (mons_near(monster))
                {
                    simple_monster_message(monster, " reads a scroll.");
                    simple_monster_message(monster, " blinks!");
                    monster_blink(monster);
                    read = true;
                }
            }
            break;

        case SCR_SUMMONING:
            if (mons_near(monster))
            {
                simple_monster_message(monster, " reads a scroll.");
                create_monster( MONS_ABOMINATION_SMALL, 2,
                                SAME_ATTITUDE(monster), monster->x, monster->y,
                                monster->foe, 250 );
                read = true;
            }
            break;
        }

        if (read)
        {
            if (dec_mitm_item_quantity( monster->inv[MSLOT_SCROLL], 1 ))
                monster->inv[MSLOT_SCROLL] = NON_ITEM;
        }

        return read;
    }
}                               // end handle_scroll()

//---------------------------------------------------------------
//
// handle_wand
//
// Give the monster a chance to zap a wand. Returns true if the
// monster zapped.
//
//---------------------------------------------------------------
static bool handle_wand(struct monsters *monster, bolt &beem)
{
    // yes, there is a logic to this ordering {dlb}:
    if (monster->behaviour == BEH_SLEEP)
        return (false);
    else if (!mons_near(monster))
        return (false);
    else if (monster->has_ench(ENCH_SUBMERGED))
        return (false);
    else if (monster->inv[MSLOT_WAND] == NON_ITEM
             || mitm[monster->inv[MSLOT_WAND]].plus <= 0)
    {
        return (false);
    }
    else if (coinflip())
    {
        bool niceWand = false;
        bool zap = false;

        // map wand type to monster spell type
        int mzap = map_wand_to_mspell(mitm[monster->inv[MSLOT_WAND]].sub_type);
        if (mzap == 0)
            return (false);

        // set up the beam
        int power = 30 + monster->hit_dice;
        bolt theBeam = mons_spells(mzap, power);

        // XXX: ugly hack this:
        static char wand_buff[ ITEMNAME_SIZE ];

        beem.name = theBeam.name;
        beem.beam_source = monster_index(monster);
        beem.source_x = monster->x;
        beem.source_y = monster->y;
        beem.colour = theBeam.colour;
        beem.range = theBeam.range;
        beem.rangeMax = theBeam.rangeMax;
        beem.damage = theBeam.damage;
        beem.ench_power = theBeam.ench_power;
        beem.hit = theBeam.hit;
        beem.type = theBeam.type;
        beem.flavour = theBeam.flavour;
        beem.thrower = theBeam.thrower;
        beem.is_beam = theBeam.is_beam;
        beem.is_explosion = theBeam.is_explosion;

        item_def item = mitm[ monster->inv[MSLOT_WAND] ];

#if HISCORE_WEAPON_DETAIL
        set_ident_flags( item, ISFLAG_IDENT_MASK );
#else
        unset_ident_flags( item, ISFLAG_IDENT_MASK );
        set_ident_flags( item, ISFLAG_KNOW_TYPE );
#endif

        item_name( item, DESC_PLAIN, wand_buff );

        beem.aux_source = wand_buff;

        switch (mitm[monster->inv[MSLOT_WAND]].sub_type)
        {
            // these have been deemed "too tricky" at this time {dlb}:
        case WAND_POLYMORPH_OTHER:
        case WAND_ENSLAVEMENT:
        case WAND_DIGGING:
        case WAND_RANDOM_EFFECTS:
            return (false);

        // these are wands that monsters will aim at themselves {dlb}:
        case WAND_HASTING:
            if (!monster->has_ench(ENCH_HASTE))
            {
                beem.target_x = monster->x;
                beem.target_y = monster->y;

                niceWand = true;
                break;
            }
            return (false);

        case WAND_HEALING:
            if (monster->hit_points <= monster->max_hit_points / 2)
            {
                beem.target_x = monster->x;
                beem.target_y = monster->y;

                niceWand = true;
                break;
            }
            return (false);

        case WAND_INVISIBILITY:
            if (!monster->has_ench(ENCH_INVIS) 
                && !monster->has_ench(ENCH_SUBMERGED)
                && (!mons_friendly(monster) || player_see_invis(false)))
            {
                beem.target_x = monster->x;
                beem.target_y = monster->y;

                niceWand = true;
                break;
            }
            return (false);

        case WAND_TELEPORTATION:
            if (monster->hit_points <= monster->max_hit_points / 2)
            {
                if (!monster->has_ench(ENCH_TP)
                    && !one_chance_in(20))
                {
                    beem.target_x = monster->x;
                    beem.target_y = monster->y;

                    niceWand = true;
                    break;
                }
                // this break causes the wand to be tried on the player:
                break;
            }
            return (false);
        }

        // fire tracer, if necessary
        if (!niceWand)
        {
            fire_tracer( monster, beem );

            // good idea?
            zap = mons_should_fire( beem );
        }

        if (niceWand || zap)
        {
            if (!simple_monster_message(monster, " zaps a wand."))
            {
                if (!silenced(you.x_pos, you.y_pos))
                    mpr("You hear a zap.", MSGCH_SOUND);
            }

            // charge expenditure {dlb}
            mitm[monster->inv[MSLOT_WAND]].plus--;
            beem.is_tracer = false;
            fire_beam( beem );

            return (true);
        }
    }

    return (false);
}                               // end handle_wand()

// Returns a suitable breath weapon for the draconian; does not handle all
// draconians, does fire a tracer.
static int get_draconian_breath_spell( struct monsters *monster )
{
    int draco_breath = MS_NO_SPELL;

    if (mons_genus( monster->type ) == MONS_DRACONIAN)
    {
        switch (draco_subspecies( monster ))
        {
        case MONS_BLACK_DRACONIAN:
            draco_breath = MS_LIGHTNING_BOLT;
            break;

        case MONS_PALE_DRACONIAN:
            draco_breath = MS_STEAM_BALL;
            break;

        case MONS_GREEN_DRACONIAN:
            draco_breath = MS_POISON_BLAST;
            break;

        case MONS_PURPLE_DRACONIAN:
            draco_breath = MS_ORB_ENERGY;
            break;

        case MONS_MOTTLED_DRACONIAN:
            draco_breath = MS_STICKY_FLAME;
            break;

        case MONS_DRACONIAN:
        case MONS_YELLOW_DRACONIAN:     // already handled as ability
        case MONS_RED_DRACONIAN:        // already handled as ability
        case MONS_WHITE_DRACONIAN:      // already handled as ability
        default:
            break;
        }

        if (draco_breath != MS_NO_SPELL)
        {
            // [ds] Check line-of-fire here. It won't happen elsewhere.
            bolt beem;
            setup_mons_cast(monster, beem, draco_breath);

            fire_tracer(monster, beem);

            if (!mons_should_fire(beem))
                draco_breath = MS_NO_SPELL;
        }

    }

    return (draco_breath);
}

static bool is_emergency_spell(const monster_spells &msp, int spell)
{
    // If the emergency spell appears early, it's probably not a dedicated
    // escape spell.
    for (int i = 0; i < 5; ++i)
        if (msp[i] == spell)
            return (false);
    return (msp[5] == spell);
}

//---------------------------------------------------------------
//
// handle_spell
//
// Give the monster a chance to cast a spell. Returns true if
// a spell was cast.
//
//---------------------------------------------------------------
static bool handle_spell( monsters *monster, bolt & beem )
{
    bool monsterNearby = mons_near(monster);
    bool finalAnswer = false;   // as in: "Is that your...?" {dlb}
    const int draco_breath = get_draconian_breath_spell(monster);

    // yes, there is a logic to this ordering {dlb}:
    if (monster->behaviour == BEH_SLEEP
        || (!mons_class_flag(monster->type, M_SPELLCASTER)
                && draco_breath == MS_NO_SPELL)
        || monster->has_ench(ENCH_SUBMERGED))
    {
        return (false);
    }

    if ((mons_class_flag(monster->type, M_ACTUAL_SPELLS)
            || mons_class_flag(monster->type, M_PRIEST))
        && (monster->has_ench(ENCH_GLOWING_SHAPESHIFTER, ENCH_SHAPESHIFTER)))
    {
        return (false);           //jmf: shapeshiftes don't get spells, just
                                  //     physical powers.
    }
    else if (monster->has_ench(ENCH_CONFUSION) 
            && !mons_class_flag(monster->type, M_CONFUSED))
    {
        return (false);
    }
    else if (monster->type == MONS_PANDEMONIUM_DEMON 
            && !monster->ghost->values[ GVAL_DEMONLORD_SPELLCASTER ])
    {
        return (false);
    }
    else if (random2(200) > monster->hit_dice + 50
            || (monster->type == MONS_BALL_LIGHTNING && coinflip()))
    {
        return (false);
    }
    else
    {
        int spell_cast = MS_NO_SPELL;
        monster_spells hspell_pass = monster->spells;

        // forces the casting of dig when player not visible - this is EVIL!
        if (!monsterNearby)
        {
            if (hspell_pass[4] == MS_DIG && monster->behaviour == BEH_SEEK)
            {
                spell_cast = MS_DIG;
                finalAnswer = true;
            }
            else if (hspell_pass[2] == MS_HEAL 
                        && monster->hit_points < monster->max_hit_points)
            {
                // The player's out of sight!  
                // Quick, let's take a turn to heal ourselves. -- bwr
                spell_cast = MS_HEAL;
                finalAnswer = true;
            }
            else if (monster->behaviour == BEH_FLEE)
            {
                // Since the player isn't around, we'll extend the monster's
                // normal fleeing choices to include the self-enchant slot.
                if (ms_useful_fleeing_out_of_sight(monster,hspell_pass[5]))
                {
                    spell_cast = hspell_pass[5];
                    finalAnswer = true;
                }
                else if (ms_useful_fleeing_out_of_sight(monster,hspell_pass[2]))
                {
                    spell_cast = hspell_pass[2];
                    finalAnswer = true;
                }
            }
            else if (monster->foe == MHITYOU)
            {
                return (false);
            }
        }

        // Promote the casting of useful spells for low-HP monsters.
        if (!finalAnswer 
            && monster->hit_points < monster->max_hit_points / 4
            && !one_chance_in(4))
        {
            // Note: There should always be at least some chance we don't
            // get here... even if the monster is on it's last HP.  That
            // way we don't have to worry about monsters infinitely casting 
            // Healing on themselves (ie orc priests). 
            if (monster->behaviour == BEH_FLEE
                && ms_low_hitpoint_cast( monster, hspell_pass[5] ))
            {
                spell_cast = hspell_pass[5];
                finalAnswer = true;
            }
            else if (ms_low_hitpoint_cast( monster, hspell_pass[2] ))
            {
                spell_cast = hspell_pass[2];
                finalAnswer = true;
            }
        }

        if (!finalAnswer)
        {
            // should monster not have selected dig by now, it never will:
            if (hspell_pass[4] == MS_DIG)
                hspell_pass[4] = MS_NO_SPELL;

            // remove healing/invis/haste if we don't need them
            int num_no_spell = 0;

            for (int i = 0; i < 6; i++)
            {
                if (hspell_pass[i] == MS_NO_SPELL)
                    num_no_spell++;    
                else if (ms_waste_of_time( monster, hspell_pass[i] ))
                {
                    hspell_pass[i] = MS_NO_SPELL;
                    num_no_spell++;
                }
            }

            // If no useful spells... cast no spell.
            if (num_no_spell == 6 && draco_breath == MS_NO_SPELL)
                return (false);

            // up to four tries to pick a spell.
            for (int loopy = 0; loopy < 4; loopy ++)
            {
                bool spellOK = false;

                // setup spell - fleeing monsters will always try to 
                // choose their emergency spell.
                if (monster->behaviour == BEH_FLEE)
                {
                    spell_cast = (one_chance_in(5) ? MS_NO_SPELL 
                                                   : hspell_pass[5]);
                }
                else
                {
                    // Randomly picking one of the non-emergency spells:
                    spell_cast = hspell_pass[random2(5)];
                }

                if (spell_cast == MS_NO_SPELL)
                    continue;

                // setup the spell
                setup_mons_cast(monster, beem, spell_cast);

                // beam-type spells requiring tracers
                if (ms_requires_tracer(spell_cast))
                {
                    fire_tracer(monster, beem);
                    // good idea?
                    if (mons_should_fire(beem))
                        spellOK = true;
                }
                else
                {
                    // all direct-effect/summoning/self-enchantments/etc
                    spellOK = true;

                    if (ms_direct_nasty(spell_cast)
                        && mons_aligned(monster_index(monster), monster->foe))
                    {
                        spellOK = false;
                    }
                    else if (monster->foe == MHITYOU || monster->foe == MHITNOT)
                    {
                        // XXX: Note the crude hack so that monsters can
                        // use ME_ALERT to target (we should really have
                        // a measure of time instead of peeking to see
                        // if the player is still there). -- bwr
                        if (!mons_player_visible( monster )
                            && (monster->target_x != you.x_pos 
                                || monster->target_y != you.y_pos
                                || coinflip()))
                        {
                            spellOK = false;
                        }
                    }
                    else if (!mons_monster_visible( monster, &menv[monster->foe] ))
                    {
                        spellOK = false;
                    }
                }

                // if not okay, then maybe we'll cast a defensive spell
                if (!spellOK)
                    spell_cast = (coinflip() ? hspell_pass[2] : MS_NO_SPELL);

                if (spell_cast != MS_NO_SPELL)
                    break;
            }
        }

        // If there's otherwise no ranged attack use the breath weapon.
        // The breath weapon is also occasionally used.
        if (draco_breath != MS_NO_SPELL 
                && (spell_cast == MS_NO_SPELL 
                    || (!is_emergency_spell(hspell_pass, spell_cast)
                        && one_chance_in(4))))
        {
            spell_cast = draco_breath;
            finalAnswer = true;
        }

        // should the monster *still* not have a spell, well, too bad {dlb}:
        if (spell_cast == MS_NO_SPELL)
            return (false);

        // Try to animate dead: if nothing rises, pretend we didn't cast it
        if (spell_cast == MS_ANIMATE_DEAD
            && !animate_dead( 100, SAME_ATTITUDE(monster), monster->foe, 0 ))
        {
            return (false);
        }

        if (monsterNearby)      // handle monsters within range of player
        {
            if (monster->type == MONS_GERYON)
            {
                if (silenced(monster->x, monster->y))
                    return (false);

                simple_monster_message( monster, " winds a great silver horn.",
                                        MSGCH_MONSTER_SPELL );
            }
            else if (mons_is_demon( monster->type ))
            {
                simple_monster_message( monster, " gestures.", 
                                        MSGCH_MONSTER_SPELL );
            }
            else
            {
                switch (monster->type)
                {
                default:
                    if (spell_cast == draco_breath)
                    {
                        if (!simple_monster_message(monster, " breathes.",
                                                    MSGCH_MONSTER_SPELL))
                        {
                            if (!silenced(monster->x, monster->y)
                                && !silenced(you.x_pos, you.y_pos))
                            {
                                mpr("You hear a roar.", MSGCH_SOUND);
                            }
                        }
                        break;
                    }

                    if (silenced(monster->x, monster->y))
                        return (false);

                    if (mons_class_flag(monster->type, M_PRIEST))
                    {
                        switch (random2(3))
                        {
                        case 0:
                            simple_monster_message( monster, 
                                                    " prays.",
                                                    MSGCH_MONSTER_SPELL );
                            break;
                        case 1:
                            simple_monster_message( monster, 
                                                    " mumbles some strange prayers.",
                                                    MSGCH_MONSTER_SPELL );
                            break;
                        case 2:
                        default:
                            simple_monster_message( monster, 
                                                    " utters an invocation.",
                                                    MSGCH_MONSTER_SPELL );
                            break;
                        }
                    }
                    else
                    {
                        switch (random2(3))
                        {
                        case 0:
                            // XXX: could be better, chosen to match the
                            // ones in monspeak.cc... has the problem 
                            // that it doesn't suggest a vocal component. -- bwr
                            if (player_monster_visible(monster))
                                simple_monster_message( monster, 
                                                        " gestures wildly.",
                                                        MSGCH_MONSTER_SPELL );
                            break;
                        case 1:
                            simple_monster_message( monster, 
                                                    " mumbles some strange words.",
                                                    MSGCH_MONSTER_SPELL );
                            break;
                        case 2:
                        default:
                            simple_monster_message( monster, 
                                                    " casts a spell.",
                                                    MSGCH_MONSTER_SPELL );
                            break;
                        }
                    }
                    break;

                case MONS_BALL_LIGHTNING:
                    monster->hit_points = -1;
                    break;

                case MONS_STEAM_DRAGON:
                case MONS_MOTTLED_DRAGON:
                case MONS_STORM_DRAGON:
                case MONS_GOLDEN_DRAGON:
                case MONS_SHADOW_DRAGON:
                case MONS_SWAMP_DRAGON:
                case MONS_SWAMP_DRAKE:
                case MONS_DEATH_DRAKE:
                case MONS_HELL_HOG:
                case MONS_SERPENT_OF_HELL:
                case MONS_QUICKSILVER_DRAGON:
                case MONS_IRON_DRAGON:
                    if (!simple_monster_message(monster, " breathes.",
                                                MSGCH_MONSTER_SPELL))
                    {
                        if (!silenced(monster->x, monster->y)
                            && !silenced(you.x_pos, you.y_pos))
                        {
                            mpr("You hear a roar.", MSGCH_SOUND);
                        }
                    }
                    break;

                case MONS_VAPOUR:
                    monster->add_ench(ENCH_SUBMERGED);
                    break;

                case MONS_BRAIN_WORM:
                case MONS_ELECTRIC_GOLEM:
                    // These don't show any signs that they're casting a spell.
                    break;

                case MONS_GREAT_ORB_OF_EYES:
                case MONS_SHINING_EYE:
                case MONS_EYE_OF_DEVASTATION:
                    simple_monster_message(monster, " gazes.", MSGCH_MONSTER_SPELL);
                    break;

                case MONS_GIANT_ORANGE_BRAIN:
                    simple_monster_message(monster, " pulsates.",
                                           MSGCH_MONSTER_SPELL);
                    break;

                case MONS_NAGA:
                case MONS_NAGA_WARRIOR:
                    simple_monster_message(monster, " spits poison.",
                                           MSGCH_MONSTER_SPELL);
                    break;
                }
            }
        }
        else                    // handle far-away monsters
        {
            if (monster->type == MONS_GERYON
                && !silenced(you.x_pos, you.y_pos))
            {
                mpr("You hear a weird and mournful sound.", MSGCH_SOUND);
            }
        }

        // FINALLY! determine primary spell effects {dlb}:
        if (spell_cast == MS_BLINK)
        {
            // why only cast blink if nearby? {dlb}
            if (monsterNearby)
            {
                simple_monster_message(monster, " blinks!");
                monster_blink(monster);
            }
            else
                return (false);
        }
        else
        {
            mons_cast(monster, beem, spell_cast);
            mmov_x = 0;
            mmov_y = 0;
        }
    } // end "if mons_class_flag(monster->type, M_SPELLCASTER) ...

    return (true);
}                               // end handle_spell()

//---------------------------------------------------------------
//
// handle_throw
//
// Give the monster a chance to throw something. Returns true if
// the monster hurled.
//
//---------------------------------------------------------------
static bool handle_throw(struct monsters *monster, bolt & beem)
{
    // yes, there is a logic to this ordering {dlb}:
    if (monster->has_ench(ENCH_CONFUSION) 
        || monster->behaviour == BEH_SLEEP
        || monster->has_ench(ENCH_SUBMERGED))
    {
        return (false);
    }

    if (mons_itemuse(monster->type) < MONUSE_OPEN_DOORS)
        return (false);

    const int mon_item = monster->inv[MSLOT_MISSILE];
    if (mon_item == NON_ITEM || !is_valid_item( mitm[mon_item] ))
        return (false);

    // don't allow offscreen throwing.. for now.
    if (monster->foe == MHITYOU && !mons_near(monster))
        return (false);

    // poor 2-headed ogres {dlb}
    if (monster->type == MONS_TWO_HEADED_OGRE || monster->type == MONS_ETTIN) 
        return (false);

    // recent addition {GDL} - monsters won't throw if they can do melee.
    // wastes valuable ammo, and most monsters are better at melee anyway.
    if (adjacent( beem.target_x, beem.target_y, monster->x, monster->y ))
        return (false);

    if (one_chance_in(5))
        return (false);

    // new (GDL) - don't throw idiotic stuff.  It's a waste of time.
    int wepClass = mitm[mon_item].base_type;
    int wepType = mitm[mon_item].sub_type;

    int weapon = monster->inv[MSLOT_WEAPON];

    int lnchClass = (weapon != NON_ITEM) ? mitm[weapon].base_type : -1;
    int lnchType  = (weapon != NON_ITEM) ? mitm[weapon].sub_type  :  0;

    bool thrown = false;
    bool launched = false;

    throw_type( lnchClass, lnchType, wepClass, wepType, launched, thrown );

    if (!launched && !thrown)
        return (false);

    // ok, we'll try it.
    setup_generic_throw( monster, beem );

    // fire tracer
    fire_tracer( monster, beem );

    // good idea?
    if (mons_should_fire( beem ))
    {
        beem.name.clear();
        return (mons_throw( monster, beem, mon_item ));
    }

    return (false);
}                               // end handle_throw()

static bool handle_monster_spell(monsters *monster, bolt &beem)
{
    // shapeshifters don't get spells
    if (!monster->has_ench( ENCH_GLOWING_SHAPESHIFTER,
                            ENCH_SHAPESHIFTER )
        || !mons_class_flag( monster->type, M_ACTUAL_SPELLS ))
    {
        if (handle_spell(monster, beem))
            return (true);
    }
    return (false);
}

// Give the monster its action energy (aka speed_increment).
static void monster_add_energy(monsters *monster)
{
    int energy_gained = (monster->speed * you.time_taken) / 10;

    // Slow monsters might get 0 here. Maybe we should factor in
    // *how* slow it is...but a 10-to-1 move ratio seems more than
    // enough.
    if ( energy_gained == 0 && monster->speed != 0 )
        energy_gained = 1;

    monster->speed_increment += energy_gained;

    if (you.slow > 0)
        monster->speed_increment += energy_gained;
}

// Do natural regeneration for monster.
static void monster_regenerate(monsters *monster)
{
    // regenerate:
    if (monster_descriptor(monster->type, MDSC_REGENERATES)
        
        || (monster->type == MONS_FIRE_ELEMENTAL 
            && (grd[monster->x][monster->y] == DNGN_LAVA
                || env.cgrid[monster->x][monster->y] == CLOUD_FIRE))

        || (monster->type == MONS_WATER_ELEMENTAL 
            && (grd[monster->x][monster->y] == DNGN_SHALLOW_WATER
                || grd[monster->x][monster->y] == DNGN_DEEP_WATER))

        || (monster->type == MONS_AIR_ELEMENTAL
            && env.cgrid[monster->x][monster->y] == EMPTY_CLOUD
            && one_chance_in(3))

        || one_chance_in(25))
    {
        heal_monster(monster, 1, false);
    }
}

static void handle_monster_move(int i, monsters *monster)
{
    bool brkk = false;
    struct bolt beem;
    FixedArray <unsigned int, 19, 19> show;

    if (monster->hit_points > monster->max_hit_points)
        monster->hit_points = monster->max_hit_points;

    // monster just summoned (or just took stairs), skip this action
    if (testbits( monster->flags, MF_JUST_SUMMONED ))
    {
        monster->flags &= ~MF_JUST_SUMMONED;
        return;
    }

    monster_add_energy(monster);

    // Handle enchantments and clouds on nonmoving monsters:
    if (monster->speed == 0) 
    {
        if (env.cgrid[monster->x][monster->y] != EMPTY_CLOUD
            && !monster->has_ench(ENCH_SUBMERGED))
        {
            mons_in_cloud( monster );
        }

        handle_enchantment( monster );
    }

    // memory is decremented here for a reason -- we only want it
    // decrementing once per monster "move"
    if (monster->foe_memory > 0)
        monster->foe_memory--;

    if (monster->type == MONS_GLOWING_SHAPESHIFTER)
        monster->add_ench(ENCH_GLOWING_SHAPESHIFTER);

    // otherwise there are potential problems with summonings
    if (monster->type == MONS_SHAPESHIFTER)
        monster->add_ench(ENCH_SHAPESHIFTER);

    // We reset batty monsters from wander to seek here, instead 
    // of in handle_behaviour() since that will be called with
    // every single movement, and we want these monsters to 
    // hit and run. -- bwr
    if (monster->foe != MHITNOT 
        && monster->behaviour == BEH_WANDER
        && testbits( monster->flags, MF_BATTY ))
    {
        monster->behaviour = BEH_SEEK;    
    }

    while (monster->speed_increment >= 80)
    {                   // The continues & breaks are WRT this.
        if (monster->type != -1 && monster->hit_points < 1)
            break;

        monster->speed_increment -= 10;

        if (env.cgrid[monster->x][monster->y] != EMPTY_CLOUD)
        {
            if (monster->has_ench(ENCH_SUBMERGED))
                break;

            if (monster->type == -1)
                break;  // problem with vortices

            mons_in_cloud(monster);

            if (monster->type == -1)
            {
                monster->speed_increment = 1;
                break;
            }
        }

        if (handle_enchantment(monster))
            continue;

        monster_regenerate(monster);

        if (mons_is_paralysed(monster))
            continue;
        
        handle_behaviour(monster);

        // submerging monsters will hide from clouds
        if (monster_can_submerge(monster->type, grd[monster->x][monster->y])
            && env.cgrid[monster->x][monster->y] != EMPTY_CLOUD)
        {
            monster->add_ench(ENCH_SUBMERGED);
        }

        if (monster->speed >= 100)
            continue;

        if (monster->type == MONS_ZOMBIE_SMALL
            || monster->type == MONS_ZOMBIE_LARGE
            || monster->type == MONS_SIMULACRUM_SMALL
            || monster->type == MONS_SIMULACRUM_LARGE
            || monster->type == MONS_SKELETON_SMALL
            || monster->type == MONS_SKELETON_LARGE)
        {
            monster->max_hit_points = monster->hit_points;
        }

        if (igrd[monster->x][monster->y] != NON_ITEM
            && (mons_itemuse(monster->type) == MONUSE_WEAPONS_ARMOUR 
                || mons_itemuse(monster->type) == MONUSE_EATS_ITEMS
                || monster->type == MONS_NECROPHAGE
                || monster->type == MONS_GHOUL))
        {
            if (handle_pickup(monster))
                continue;
        }

        // calculates mmov_x, mmov_y based on monster target.
        handle_movement(monster);

        brkk = false;

        if (mons_is_confused( monster )
            || (monster->type == MONS_AIR_ELEMENTAL 
                && monster->has_ench(ENCH_SUBMERGED)))
        {
            std::vector<coord_def> moves;

            int pfound = 0;
            for (int yi = -1; yi <= 1; ++yi)
            {
                for (int xi = -1; xi <= 1; ++xi)
                {
                    coord_def c = monster->pos() + coord_def(xi, yi);
                    if (in_bounds(c) && !grid_is_solid(grd(c))
                        && one_chance_in(++pfound))
                    {
                        mmov_x = xi;
                        mmov_y = yi;
                    }
                }
            }

            if (random2(2 + pfound) < 2)
                mmov_x = mmov_y = 0;

            // bounds check: don't let confused monsters try to run
            // off the map
            if (monster->x + mmov_x < 0 
                    || monster->x + mmov_x >= GXM)
            {
                mmov_x = 0;
            }

            if (monster->y + mmov_y < 0 
                    || monster->y + mmov_y >= GYM)
            {
                mmov_y = 0;
            }

            if (grid_is_solid(
                    grd[ monster->x + mmov_x ][ monster->y + mmov_y ]))
            {
                mmov_x = mmov_y = 0;
            }

            if (mgrd[monster->x + mmov_x][monster->y + mmov_y] != NON_MONSTER
                && (mmov_x != 0 || mmov_y != 0))
            {
                monsters_fight(
                    i,
                    mgrd[monster->x + mmov_x][monster->y + mmov_y]);
                
                brkk = true;
                mmov_x = 0;
                mmov_y = 0;
            }
        }

        if (brkk)
            continue;

        handle_nearby_ability( monster );

        beem.target_x = monster->target_x;
        beem.target_y = monster->target_y;

        if (monster->behaviour != BEH_SLEEP
            && monster->behaviour != BEH_WANDER)
        {
            // prevents unfriendlies from nuking you from offscreen.
            // How nice!
            if (mons_friendly(monster) || mons_near(monster))
            {
                // [ds] Special abilities shouldn't overwhelm spellcasting
                // in monsters that have both. This aims to give them both
                // roughly the same weight.

                if (coinflip()?
                        handle_special_ability(monster, beem)
                            || handle_monster_spell(monster, beem)
                    :   handle_monster_spell(monster, beem)
                            || handle_special_ability(monster, beem))
                {
                    continue;
                }

                if (handle_potion(monster, beem))
                    continue;

                if (handle_scroll(monster))
                    continue;

                if (handle_wand(monster, beem))
                    continue;

                if (handle_reaching(monster))
                    continue;
            }

            if (handle_throw(monster, beem))
                continue;
        }

        // see if we move into (and fight) an unfriendly monster
        int targmon = mgrd[monster->x + mmov_x][monster->y + mmov_y];
        if (targmon != NON_MONSTER
            && targmon != i 
            && !mons_aligned(i, targmon))
        {
            // figure out if they fight
            if (monsters_fight(i, targmon))
            {
                if (testbits(monster->flags, MF_BATTY))
                {
                    monster->behaviour = BEH_WANDER;
                    monster->target_x = 10 + random2(GXM - 10);
                    monster->target_y = 10 + random2(GYM - 10);
                    // monster->speed_increment -= monster->speed;
                }

                mmov_x = 0;
                mmov_y = 0;
                brkk = true;
            }
        }

        if (brkk)
            continue;

        if (monster->x + mmov_x == you.x_pos
            && monster->y + mmov_y == you.y_pos)
        {
            bool isFriendly = mons_friendly(monster);
            bool attacked = false;

            if (!isFriendly)
            {
                monster_attack(i);
                attacked = true;

                if (testbits(monster->flags, MF_BATTY))
                {
                    monster->behaviour = BEH_WANDER;
                    monster->target_x = 10 + random2(GXM - 10);
                    monster->target_y = 10 + random2(GYM - 10);
                }
            }

            if ((monster->type == MONS_GIANT_SPORE
                    || monster->type == MONS_BALL_LIGHTNING)
                && monster->hit_points < 1)
            {

                // detach monster from the grid first, so it
                // doesn't get hit by its own explosion (GDL)
                mgrd[monster->x][monster->y] = NON_MONSTER;

                spore_goes_pop(monster);
                monster_cleanup(monster);
                continue;
            }

            if (attacked)
            {
                mmov_x = 0;
                mmov_y = 0;
                continue;   //break;
            }
        }

        if (invalid_monster(monster) || mons_is_stationary(monster))
            continue;

        monster_move(monster);

        // reevaluate behaviour, since the monster's
        // surroundings have changed (it may have moved,
        // or died for that matter.  Don't bother for
        // dead monsters.  :)
        if (monster->type != -1)
            handle_behaviour(monster);

    }                   // end while

    if (monster->type != -1 && monster->hit_points < 1)
    {
        if (monster->type == MONS_GIANT_SPORE
            || monster->type == MONS_BALL_LIGHTNING)
        {
            // detach monster from the grid first, so it
            // doesn't get hit by its own explosion (GDL)
            mgrd[monster->x][monster->y] = NON_MONSTER;

            spore_goes_pop( monster );
            monster_cleanup( monster );
            return;
        }
        else
        {
            monster_die( monster, KILL_MISC, 0 );
        }
    }    
}

//---------------------------------------------------------------
//
// handle_monsters
//
// This is the routine that controls monster AI.
//
//---------------------------------------------------------------
void handle_monsters(void)
{
    // Keep track of monsters that have already moved and don't allow
    // them to move again.
    memset(immobile_monster, 0, sizeof immobile_monster);

    for (int i = 0; i < MAX_MONSTERS; i++)
    {
        struct monsters *monster = &menv[i];

        if (monster->type == -1 || immobile_monster[i])
            continue;

        const int mx = monster->x, 
                  my = monster->y;
        handle_monster_move(i, monster);

        if (!invalid_monster(monster) 
                && (monster->x != mx || monster->y != my))
            immobile_monster[i] = true;

        // If the player got banished, discard pending monster actions.
        if (you.banished)
            break;
    }                           // end of for loop

    // Clear any summoning flags so that lower indiced 
    // monsters get their actions in the next round.
    for (int i = 0; i < MAX_MONSTERS; i++)
    {
        menv[i].flags &= ~MF_JUST_SUMMONED;
    }
}                               // end handle_monster()


//---------------------------------------------------------------
//
// handle_pickup
//
// Returns false if monster doesn't spend any time pickup up
//
//---------------------------------------------------------------
static bool handle_pickup(struct monsters *monster)
{
    // single calculation permissible {dlb}
    char str_pass[ ITEMNAME_SIZE ];
    bool monsterNearby = mons_near(monster);
    int  item = NON_ITEM;

    if (monster->has_ench(ENCH_SUBMERGED))
        return (false);

    if (monster->behaviour == BEH_SLEEP)
        return (false);

    if (mons_itemuse(monster->type) == MONUSE_EATS_ITEMS)
    {
        int hps_gained = 0;
        int max_eat = roll_dice( 1, 10 );
        int eaten = 0;

        for (item = igrd[monster->x][monster->y]; 
             item != NON_ITEM && eaten < max_eat && hps_gained < 50;
             item = mitm[item].link)
        {
            int quant = mitm[item].quantity;

            // don't eat artefacts (note that unrandarts are randarts)
            if (is_fixed_artefact(mitm[item]) ||
                is_random_artefact(mitm[item]))
                continue;

            // shouldn't eat stone things
            //    - but what about wands and rings?
            if (mitm[item].base_type == OBJ_MISSILES
                && (mitm[item].sub_type == MI_STONE
                    || mitm[item].sub_type == MI_LARGE_ROCK))
            {
                continue;
            }

            // don't eat special game items
            if (mitm[item].base_type == OBJ_ORBS
                || (mitm[item].base_type == OBJ_MISCELLANY 
                    && mitm[item].sub_type == MISC_RUNE_OF_ZOT))
            {
                continue;
            }

            if (mitm[igrd[monster->x][monster->y]].base_type != OBJ_GOLD)
            {
                if (quant > max_eat - eaten)
                    quant = max_eat - eaten;

                hps_gained += (quant * item_mass( mitm[item] )) / 20 + quant;
                eaten += quant;
            }
            else
            {
                // shouldn't be much trouble to digest a huge pile of gold!
                if (quant > 500)
                    quant = 500 + roll_dice( 2, (quant - 500) / 2 );

                hps_gained += quant / 10 + 1;
                eaten++;
            }

            dec_mitm_item_quantity( item, quant );
        }

        if (eaten)
        {
            if (hps_gained < 1)
                hps_gained = 1;
            else if (hps_gained > 50)
                hps_gained = 50;

            // This is done manually instead of using heal_monster(),
            // because that function doesn't work quite this way.  -- bwr
            monster->hit_points += hps_gained;

            if (monster->max_hit_points < monster->hit_points)
                monster->max_hit_points = monster->hit_points;

            if (!silenced(you.x_pos, you.y_pos)
                && !silenced(monster->x, monster->y))
            {
                strcpy(info, "You hear a");
                if (!monsterNearby)
                    strcat(info, " distant");
                strcat(info, " slurping noise.");
                mpr(info, MSGCH_SOUND);
            }

            if (mons_class_flag( monster->type, M_SPLITS )) 
            {
                const int reqd = (monster->hit_dice <= 6) 
                                            ? 50 : monster->hit_dice * 8;

                if (monster->hit_points >= reqd)
                    jelly_divide(monster);
            }
        }

        return (false);
    }                           // end "if jellies"

    // Note: Monsters only look at top of stacks.
    item = igrd[monster->x][monster->y];

    switch (mitm[item].base_type)
    {
    case OBJ_WEAPONS:
        if (monster->inv[MSLOT_WEAPON] != NON_ITEM)
            return (false);

        if (is_fixed_artefact( mitm[item] ))
            return (false);

        if (is_random_artefact( mitm[item] ))
            return (false);

        // wimpy monsters (Kob, gob) shouldn't pick up halberds etc
        // of course, this also block knives {dlb}:
        if ((mons_species(monster->type) == MONS_KOBOLD
                || mons_species(monster->type) == MONS_GOBLIN)
            && property( mitm[item], PWPN_HIT ) <= 0)
        {
            return (false);
        }

        // Nobody picks up giant clubs:
        if (mitm[item].sub_type == WPN_GIANT_CLUB
            || mitm[item].sub_type == WPN_GIANT_SPIKED_CLUB)
        {
            return (false);
        }

        monster->inv[MSLOT_WEAPON] = item;

        if (get_weapon_brand(mitm[monster->inv[MSLOT_WEAPON]]) == SPWPN_PROTECTION)
        {
            monster->ac += 3;
        }

        if (monsterNearby)
        {
            strcpy(info, ptr_monam(monster, DESC_CAP_THE));
            strcat(info, " picks up ");
            it_name(monster->inv[MSLOT_WEAPON], DESC_NOCAP_A, str_pass);
            strcat(info, str_pass);
            strcat(info, ".");
            mpr(info);
        }
        break;

    case OBJ_MISSILES:
        // don't pick up if we're in combat, and there isn't much there
        if (mitm[item].quantity < 5 || monster->behaviour != BEH_WANDER)
            return (false);

        if (monster->inv[MSLOT_MISSILE] != NON_ITEM
            && mitm[monster->inv[MSLOT_MISSILE]].sub_type == mitm[item].sub_type
            && mitm[monster->inv[MSLOT_MISSILE]].plus == mitm[item].plus
            && mitm[monster->inv[MSLOT_MISSILE]].special == mitm[item].special)
        {
            if (monsterNearby)
            {
                strcpy(info, ptr_monam(monster, DESC_CAP_THE));
                strcat(info, " picks up ");
                it_name(item, DESC_NOCAP_A, str_pass);
                strcat(info, str_pass);
                strcat(info, ".");
                mpr(info);
            }

            inc_mitm_item_quantity( monster->inv[MSLOT_MISSILE], 
                                    mitm[item].quantity );

            dec_mitm_item_quantity( item, mitm[item].quantity );
            return (true);
        }

        // nobody bothers to pick up rocks if they don't already have some:
        if (mitm[item].sub_type == MI_LARGE_ROCK)
            return (false);

        // monsters with powerful melee attacks don't bother
        if (mons_damage(monster->type, 0) > 5)
            return (false);

        monster->inv[MSLOT_MISSILE] = item;

        if (monsterNearby)
        {
            strcpy(info, ptr_monam(monster, DESC_CAP_THE));
            strcat(info, " picks up ");
            it_name(monster->inv[MSLOT_MISSILE], DESC_NOCAP_A, str_pass);
            strcat(info, str_pass);
            strcat(info, ".");
            mpr(info);
        }
        break;

    case OBJ_WANDS:
        if (monster->inv[MSLOT_WAND] != NON_ITEM)
            return (false);

        monster->inv[MSLOT_WAND] = item;

        if (monsterNearby)
        {
            strcpy(info, ptr_monam(monster, DESC_CAP_THE));
            strcat(info, " picks up ");
            it_name(monster->inv[MSLOT_WAND], DESC_NOCAP_A, str_pass);
            strcat(info, str_pass);
            strcat(info, ".");
            mpr(info);
        }
        break;

    case OBJ_SCROLLS:
        if (monster->inv[MSLOT_SCROLL] != NON_ITEM)
            return (false);

        monster->inv[MSLOT_SCROLL] = item;

        if (monsterNearby)
        {
            strcpy(info, ptr_monam(monster, DESC_CAP_THE));
            strcat(info, " picks up ");
            it_name(monster->inv[MSLOT_SCROLL], DESC_NOCAP_A, str_pass);
            strcat(info, str_pass);
            strcat(info, ".");
            mpr(info);
        }
        break;

    case OBJ_POTIONS:
        if (monster->inv[MSLOT_POTION] != NON_ITEM)
            return (false);

        monster->inv[MSLOT_POTION] = item;

        if (monsterNearby)
        {
            strcpy(info, ptr_monam(monster, DESC_CAP_THE));
            strcat(info, " picks up ");
            it_name(monster->inv[MSLOT_POTION], DESC_NOCAP_A, str_pass);
            strcat(info, str_pass);
            strcat(info, ".");
            mpr(info);
        }
        break;

    case OBJ_CORPSES:
        if (monster->type != MONS_NECROPHAGE && monster->type != MONS_GHOUL)
            return (false);

        monster->hit_points += 1 + random2(mons_weight(mitm[item].plus)) / 100;

        // limited growth factor here -- should 77 really be the cap? {dlb}:
        if (monster->hit_points > 100)
            monster->hit_points = 100;

        if (monster->hit_points > monster->max_hit_points)
            monster->max_hit_points = monster->hit_points;

        if (monsterNearby)
        {
            strcpy(info, ptr_monam(monster, DESC_CAP_THE));
            strcat(info, " eats ");
            it_name(item, DESC_NOCAP_THE, str_pass);
            strcat(info, str_pass);
            strcat(info, ".");
            mpr(info);
        }

        destroy_item( item );
        return (true);

    case OBJ_GOLD: //mv - monsters now pick up gold (19 May 2001)
        if (monsterNearby)
        {
            mprf("%s picks up some gold.",
                 monam( monster, monster->number, monster->type, 
                        player_monster_visible( monster ), 
                        DESC_CAP_THE ));
        }

        if (monster->inv[MSLOT_GOLD] != NON_ITEM)
        {
            // transfer gold to monster's object, destroy ground object
            inc_mitm_item_quantity( monster->inv[MSLOT_GOLD], 
                                    mitm[item].quantity );

            destroy_item( item );
            return (true);
        }
        else
        {
            monster->inv[MSLOT_GOLD] = item;
        }
        break;

    default:
        return (false);
    }

    // Item has been picked-up, move to monster inventory.
    mitm[item].x = 0; 
    mitm[item].y = 0; 

    // Monster's only take the top item of stacks, so relink the 
    // top item, and unlink the item.
    igrd[monster->x][monster->y] = mitm[item].link;
    mitm[item].link = NON_ITEM;

    if (monster->speed_increment > 25)
        monster->speed_increment -= monster->speed;

    return (true);
}                               // end handle_pickup()

static void jelly_grows(monsters *monster)
{
    if (!silenced(you.x_pos, you.y_pos)
        && !silenced(monster->x, monster->y))
    {
        strcpy(info, "You hear a");
        if (!mons_near(monster))
            strcat(info, " distant");
        strcat(info, " slurping noise.");
        mpr(info, MSGCH_SOUND);
    }

    monster->hit_points += 5;

    // note here, that this makes jellies "grow" {dlb}:
    if (monster->hit_points > monster->max_hit_points)
        monster->max_hit_points = monster->hit_points;

    if (mons_class_flag( monster->type, M_SPLITS ))
    {
        // and here is where the jelly might divide {dlb}
        const int reqd = (monster->hit_dice < 6) ? 50 
                                                 : monster->hit_dice * 8;

        if (monster->hit_points >= reqd)
            jelly_divide(monster);
    }
}

static bool mons_can_displace(const monsters *mpusher, const monsters *mpushee)
{
    if (invalid_monster(mpusher) || invalid_monster(mpushee))
        return (false);

    const int ipushee = monster_index(mpushee);
    if (ipushee < 0 || ipushee >= MAX_MONSTERS)
        return (false);

    if (immobile_monster[ipushee])
        return (false);

    // Confused monsters can't be pushed past, sleeping monsters
    // can't push. Note that sleeping monsters can't be pushed
    // past, either, but they may be woken up by a crowd trying to
    // elbow past them, and the wake-up check happens downstream.
    if (mons_is_confused(mpusher) || mons_is_confused(mpushee)
        || mons_is_paralysed(mpusher) || mons_is_paralysed(mpushee)
        || mons_is_sleeping(mpusher) || mons_is_stationary(mpusher)
        || mons_is_stationary(mpushee))
    {
        return (false);
    }

    // Batty monsters are unpushable
    if (mons_is_batty(mpusher) || mons_is_batty(mpushee))
        return (false);

    if (!monster_shover(mpusher))
        return (false);

    if (!monster_senior(mpusher, mpushee))
        return (false);

    return (true);
}

static bool monster_swaps_places( monsters *mon, int mx, int my )
{
    if (!mx && !my)
        return (false);

    int targmon = mgrd[mon->x + mx][mon->y + my];    
    if (targmon == MHITNOT || targmon == MHITYOU)
        return (false);

    monsters *m2 = &menv[targmon];
    if (!mons_can_displace(mon, m2))
        return (false);

    if (mons_is_sleeping(m2))
    {
        if (one_chance_in(2))
        {
#ifdef DEBUG_DIAGNOSTICS
            char mname[ITEMNAME_SIZE];
            moname(m2->type, true, DESC_PLAIN, mname);
            mprf(MSGCH_DIAGNOSTICS, 
                    "Alerting monster %s at (%d,%d)", mname, m2->x, m2->y);
#endif
            behaviour_event( m2, ME_ALERT, MHITNOT );
        }
        return (false);
    }

    // Check that both monsters will be happy at their proposed new locations.
    const int cx = mon->x, cy = mon->y,
              nx = mon->x + mx, ny = mon->y + my;
    if (!habitat_okay(mon, grd[nx][ny])
            || !habitat_okay(m2, grd[cx][cy]))
        return (false);

    // Okay, do the swap!
    mon->x = nx;
    mon->y = ny;
    mgrd[nx][ny] = monster_index(mon);

    m2->x  = cx;
    m2->y  = cy;
    const int m2i = monster_index(m2);
    ASSERT(m2i >= 0 && m2i < MAX_MONSTERS);
    mgrd[cx][cy] = m2i;
    immobile_monster[m2i] = true;

    mons_trap(mon);
    mons_trap(m2);

    return (false);
}

static void do_move_monster(monsters *monster, int xi, int yi)
{
    const int fx = monster->x + xi,
        fy = monster->y + yi;

    if (!in_bounds(fx, fy))
        return;

    if (fx == you.x_pos && fy == you.y_pos)
    {
        monster_attack( monster_index(monster) );
        return;
    }
    
    if (!xi && !yi)
    {
        const int mx = monster_index(monster);
        monsters_fight( mx, mx );
        return;
    }

    if (mgrd[fx][fy] != NON_MONSTER)
    {
        monsters_fight( monster_index(monster), mgrd[fx][fy] );
        return;
    }

    if (!xi && !yi)
        return;

    mgrd[monster->x][monster->y] = NON_MONSTER;
    
    /* this appears to be the real one, ie where the movement occurs: */
    monster->x = fx;
    monster->y = fy;
    
    if (monster->type == MONS_CURSE_TOE)
    {
        // Curse toes are a special case; they can only move at half their
        // attack rate. To simulate that, the toe loses more energy.
        monster->speed_increment -= 5;
    }
    
    /* need to put in something so that monster picks up multiple
       items (eg ammunition) identical to those it's carrying. */
    mgrd[monster->x][monster->y] = monster_index(monster);

    // monsters stepping on traps:
    mons_trap(monster);

    if (monster->alive())
        mons_check_pool(monster);
}

void mons_check_pool(monsters *mons, int killer)
{
    // Levitating/flying monsters don't make contact with the terrain.
    const int lev = mons->levitates();
    if (lev == 2 || (lev && !mons->paralysed()))
        return;
    
    const int grid = grd(mons->pos());
    if ((grid == DNGN_LAVA || grid == DNGN_DEEP_WATER)
        && !monster_habitable_grid(mons, grid))
    {
        const bool message = mons_near(mons);
        
        // don't worry about invisibility - you should be able to
        // see if something has fallen into the lava
        if (message)
            mprf("%s falls into the %s!",
                 ptr_monam(mons, DESC_CAP_THE),
                 (grid == DNGN_LAVA ? "lava" : "water"));

        // Even fire resistant monsters perish in lava, but undead can survive
        // deep water.
        if (grid == DNGN_LAVA || mons->holiness() != MH_UNDEAD)
        {
            if (message)
            {
                if (grid == DNGN_LAVA)
                    simple_monster_message(
                        mons, " is incinerated!",
                        MSGCH_MONSTER_DAMAGE, MDAM_DEAD);
                else
                    simple_monster_message(
                        mons, " drowns.",
                        MSGCH_MONSTER_DAMAGE, MDAM_DEAD);
            }
            
            monster_die(mons, killer, 0, true);
        }
    }
}

static void monster_move(struct monsters *monster)
{
    FixedArray < bool, 3, 3 > good_move;
    int count_x, count_y, count;
    int okmove = DNGN_SHALLOW_WATER;

    const int habitat = monster_habitat( monster->type ); 
    bool deep_water_available = false;

    if (monster->confused())
    {
        if (mmov_x || mmov_y || one_chance_in(15))
            do_move_monster(monster, mmov_x, mmov_y);
        return;
    }
    
    // let's not even bother with this if mmov_x and mmov_y are zero.
    if (mmov_x == 0 && mmov_y == 0)
        return;

    // effectively slows down monster movement across water.
    // Fire elementals can't cross at all.
    if (monster->type == MONS_FIRE_ELEMENTAL || one_chance_in(5))
        okmove = DNGN_WATER_STUCK;

    if (mons_flies(monster) > 0 
        || habitat != DNGN_FLOOR
        || mons_class_flag( monster->type, M_AMPHIBIOUS ))
    {
        okmove = MINMOVE;
    }

    for (count_x = 0; count_x < 3; count_x++)
    {
        for (count_y = 0; count_y < 3; count_y++)
        {
            good_move[count_x][count_y] = true;
            
            const int targ_x = monster->x + count_x - 1;
            const int targ_y = monster->y + count_y - 1;
            // [ds] Bounds check was after grd[targ_x][targ_y] which would
            // trigger an ASSERT. Moved it up.

            // bounds check - don't consider moving out of grid!
            if (targ_x < 0 || targ_x >= GXM || targ_y < 0 || targ_y >= GYM)
            {
                good_move[count_x][count_y] = false;
                continue;
            }

            int target_grid = grd[targ_x][targ_y];

            const int targ_cloud_num  = env.cgrid[ targ_x ][ targ_y ];
            const int targ_cloud_type = 
                targ_cloud_num == EMPTY_CLOUD? CLOUD_NONE
                                             : env.cloud[targ_cloud_num].type;

            const int curr_cloud_num = env.cgrid[ monster->x ][ monster->y ];
            const int curr_cloud_type =
                curr_cloud_num == EMPTY_CLOUD? CLOUD_NONE
                                             : env.cloud[curr_cloud_num].type;

            if (target_grid == DNGN_DEEP_WATER)
                deep_water_available = true;

            if (monster->type == MONS_BORING_BEETLE
                && target_grid == DNGN_ROCK_WALL)
            {
                // don't burrow out of bounds
                if (targ_x <= 7 || targ_x >= (GXM - 8)
                    || targ_y <= 7 || targ_y >= (GYM - 8))
                {
                    good_move[count_x][count_y] = false;
                    continue;
                }

                // don't burrow at an angle (legacy behaviour)
                if (count_x != 1 && count_y != 1)
                {
                    good_move[count_x][count_y] = false;
                    continue;
                }
            } 
            else if (grd[ targ_x ][ targ_y ] < okmove)
            {
                good_move[count_x][count_y] = false;
                continue;
            }
            else if (!habitat_okay( monster, target_grid ))
            {
                good_move[count_x][count_y] = false;
                continue;
            }

            if (monster->type == MONS_WANDERING_MUSHROOM
                && see_grid(targ_x, targ_y))
            {
                good_move[count_x][count_y] = false;
                continue;
            }

            // Water elementals avoid fire and heat
            if (monster->type == MONS_WATER_ELEMENTAL
                && (target_grid == DNGN_LAVA
                    || targ_cloud_type == CLOUD_FIRE 
                    || targ_cloud_type == CLOUD_STEAM))
            {
                good_move[count_x][count_y] = false;
                continue;
            }

            // Fire elementals avoid water and cold 
            if (monster->type == MONS_FIRE_ELEMENTAL
                && (target_grid == DNGN_DEEP_WATER
                    || target_grid == DNGN_SHALLOW_WATER
                    || target_grid == DNGN_BLUE_FOUNTAIN
                    || targ_cloud_type == CLOUD_COLD))
            {
                good_move[count_x][count_y] = false;
                continue;
            }

            // Submerged water creatures avoid the shallows where
            // they would be forced to surface. -- bwr
            // [dshaligram] Monsters now prefer to head for deep water only if
            // they're low on hitpoints. No point in hiding if they want a
            // fight.
            if (habitat == DNGN_DEEP_WATER
                && (targ_x != you.x_pos || targ_y != you.y_pos)
                && target_grid != DNGN_DEEP_WATER
                && grd[monster->x][monster->y] == DNGN_DEEP_WATER
                && monster->hit_points < (monster->max_hit_points * 3) / 4)
            {
                good_move[count_x][count_y] = false;
                continue;
            }

            // smacking the player is always a good move if
            // we're hostile (even if we're heading somewhere
            // else)

            // smacking another monster is good, if the monsters
            // are aligned differently
            if (mgrd[targ_x][targ_y] != NON_MONSTER)
            {
                const int thismonster = monster_index(monster),
                          targmonster = mgrd[targ_x][targ_y];
                if (mons_aligned(thismonster, targmonster)
                        && targmonster != MHITNOT
                        && targmonster != MHITYOU
                        && !mons_can_displace(monster, &menv[targmonster]))
                {
                    good_move[count_x][count_y] = false;
                    continue;
                }
            }

            // wandering through a trap is OK if we're pretty healthy,
            // really stupid, or immune to the trap
            const int which_trap = trap_at_xy(targ_x,targ_y);
            if ( which_trap >= 0 &&
                 mons_intel(monster->type) != I_PLANT &&
                 monster->hit_points < monster->max_hit_points / 2 &&
                 (!mons_flies(monster) ||
                  trap_category(env.trap[which_trap].type) != DNGN_TRAP_MECHANICAL) )
            {
                good_move[count_x][count_y] = false;
                continue;
            }

            if (targ_cloud_num != EMPTY_CLOUD)
            {
                if (curr_cloud_num != EMPTY_CLOUD
                    && targ_cloud_type == curr_cloud_type)
                {
                    continue;
                }

                switch (targ_cloud_type)
                {
                case CLOUD_FIRE:
                    if (mons_res_fire(monster) > 0)
                        continue;

                    if (monster->hit_points >= 15 + random2avg(46, 5))
                        continue;
                    break;

                case CLOUD_STINK:
                    if (mons_res_poison(monster) > 0)
                        continue;
                    if (1 + random2(5) < monster->hit_dice)
                        continue;
                    if (monster->hit_points >= random2avg(19, 2))
                        continue;
                    break;

                case CLOUD_COLD:
                    if (mons_res_cold(monster) > 0)
                        continue;

                    if (monster->hit_points >= 15 + random2avg(46, 5))
                        continue;
                    break;

                case CLOUD_POISON:
                    if (mons_res_poison(monster) > 0)
                        continue;

                    if (monster->hit_points >= random2avg(37, 4))
                        continue;
                    break;

                // this isn't harmful, but dumb critters might think so.
                case CLOUD_GREY_SMOKE:
                    if (mons_intel(monster->type) > I_ANIMAL || coinflip())
                        continue;

                    if (mons_res_fire(monster) > 0)
                        continue;

                    if (monster->hit_points >= random2avg(19, 2))
                        continue;
                    break;

                default:
                    continue;   // harmless clouds
                }

                // if we get here, the cloud is potentially harmful.
                // exceedingly dumb creatures will still wander in.
                if (mons_intel(monster->type) != I_PLANT)
                    good_move[count_x][count_y] = false;
            }
        }
    } // now we know where we _can_ move.

    // normal/smart monsters know about secret doors (they _live_ in the
    // dungeon!)
    if (grd[monster->x + mmov_x][monster->y + mmov_y] == DNGN_CLOSED_DOOR
        || (grd[monster->x + mmov_x][monster->y + mmov_y] == DNGN_SECRET_DOOR
            && (mons_intel(monster_index(monster)) == I_HIGH
                || mons_intel(monster_index(monster)) == I_NORMAL)))
    {
        if (monster->type == MONS_ZOMBIE_SMALL
            || monster->type == MONS_ZOMBIE_LARGE
            || monster->type == MONS_SIMULACRUM_SMALL
            || monster->type == MONS_SIMULACRUM_LARGE
            || monster->type == MONS_SKELETON_SMALL
            || monster->type == MONS_SKELETON_LARGE
            || monster->type == MONS_SPECTRAL_THING)
        {
            // for zombies, monster type is kept in mon->number
            if (mons_itemuse(monster->number) >= MONUSE_OPEN_DOORS)
            {
                grd[monster->x + mmov_x][monster->y + mmov_y] = DNGN_OPEN_DOOR;
                return;
            }
        }
        else if (mons_itemuse(monster->type) >= MONUSE_OPEN_DOORS)
        {
            grd[monster->x + mmov_x][monster->y + mmov_y] = DNGN_OPEN_DOOR;
            return;
        }
    } // endif - secret/closed doors

    // jellies eat doors.  Yum!
    if ((grd[monster->x + mmov_x][monster->y + mmov_y] == DNGN_CLOSED_DOOR
            || grd[monster->x + mmov_x][monster->y + mmov_y] == DNGN_OPEN_DOOR)
        && mons_itemuse(monster->type) == MONUSE_EATS_ITEMS)
    {
        grd[monster->x + mmov_x][monster->y + mmov_y] = DNGN_FLOOR;

        jelly_grows(monster);
    } // done door-eating jellies


    // water creatures have a preference for water they can hide in -- bwr
    // [ds] Weakened the powerful attraction to deep water if the monster
    // is in good health.
    if (habitat == DNGN_DEEP_WATER 
        && deep_water_available
        && grd[monster->x][monster->y] != DNGN_DEEP_WATER
        && grd[monster->x + mmov_x][monster->y + mmov_y] != DNGN_DEEP_WATER
        && (monster->x + mmov_x != you.x_pos 
            || monster->y + mmov_y != you.y_pos)
        && (one_chance_in(3)
            || monster->hit_points <= (monster->max_hit_points * 3) / 4))
    {
        count = 0;

        for (count_x = 0; count_x < 3; count_x++)
        {
            for (count_y = 0; count_y < 3; count_y++)
            {
                if (good_move[count_x][count_y]
                    && grd[monster->x + count_x - 1][monster->y + count_y - 1]
                            == DNGN_DEEP_WATER)
                {
                    count++;

                    if (one_chance_in( count ))
                    {
                        mmov_x = count_x - 1;  
                        mmov_y = count_y - 1; 
                    }
                }
            }
        }
    }


    // now, if a monster can't move in its intended direction, try
    // either side.  If they're both good, move in whichever dir
    // gets it closer(farther for fleeing monsters) to its target.
    // If neither does, do nothing.
    if (good_move[mmov_x + 1][mmov_y + 1] == false)
    {
        int current_distance = grid_distance( monster->x, monster->y,
                                              monster->target_x, 
                                              monster->target_y );

        int dir = -1;
        int i, mod, newdir;

        for (i = 0; i < 8; i++)
        {
            if (compass_x[i] == mmov_x && compass_y[i] == mmov_y)
            {
                dir = i;
                break;
            }
        }

        if (dir < 0)
            goto forget_it;

        int dist[2];

        // first 1 away, then 2 (3 is silly)
        for (int j = 1; j <= 2; j++)
        {
            int sdir, inc;

            if (coinflip())
            {
                sdir = -j;
                inc = 2*j;
            }
            else
            {
                sdir = j;
                inc = -2*j;
            }

            // try both directions
            for (mod = sdir, i = 0; i < 2; mod += inc, i++)
            {
                newdir = (dir + 8 + mod) % 8;
                if (good_move[compass_x[newdir] + 1][compass_y[newdir] + 1])
                {
                    dist[i] = grid_distance( monster->x + compass_x[newdir],
                                             monster->y + compass_y[newdir],
                                             monster->target_x, 
                                             monster->target_y );
                }
                else
                {
                    dist[i] = (monster->behaviour == BEH_FLEE) ? (-FAR_AWAY)
                                                               : FAR_AWAY;
                }
            }

            // now choose
            if (dist[0] == dist[1] && abs(dist[0]) == FAR_AWAY)
                continue;

            // which one was better? -- depends on FLEEING or not
            if (monster->behaviour == BEH_FLEE)
            {
                if (dist[0] >= dist[1] && dist[0] >= current_distance)
                {
                    mmov_x = compass_x[((dir+8)+sdir)%8];
                    mmov_y = compass_y[((dir+8)+sdir)%8];
                    break;
                }
                if (dist[1] >= dist[0] && dist[1] >= current_distance)
                {
                    mmov_x = compass_x[((dir+8)-sdir)%8];
                    mmov_y = compass_y[((dir+8)-sdir)%8];
                    break;
                }
            }
            else
            {
                if (dist[0] <= dist[1] && dist[0] <= current_distance)
                {
                    mmov_x = compass_x[((dir+8)+sdir)%8];
                    mmov_y = compass_y[((dir+8)+sdir)%8];
                    break;
                }
                if (dist[1] <= dist[0] && dist[1] <= current_distance)
                {
                    mmov_x = compass_x[((dir+8)-sdir)%8];
                    mmov_y = compass_y[((dir+8)-sdir)%8];
                    break;
                }
            }
        }
    } // end - try to find good alternate move

forget_it:

    // ------------------------------------------------------------------
    // if we haven't found a good move by this point, we're not going to.
    // ------------------------------------------------------------------

    // take care of beetle burrowing
    if (monster->type == MONS_BORING_BEETLE)
    {
        if (grd[monster->x + mmov_x][monster->y + mmov_y] == DNGN_ROCK_WALL
            && good_move[mmov_x + 1][mmov_y + 1] == true)
        {
            grd[monster->x + mmov_x][monster->y + mmov_y] = DNGN_FLOOR;

            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear a grinding noise.", MSGCH_SOUND);
        }
    }

    if (good_move[mmov_x + 1][mmov_y + 1] && !(mmov_x == 0 && mmov_y == 0))
    {
        // check for attacking player
        if (monster->x + mmov_x == you.x_pos
            && monster->y + mmov_y == you.y_pos)
        {
            monster_attack( monster_index(monster) );
            mmov_x = 0;
            mmov_y = 0;
        }

        // If we're following the player through stairs, the only valid 
        // movement is towards the player. -- bwr
        if (testbits( monster->flags, MF_TAKING_STAIRS ))
        {
#if DEBUG_DIAGNOSTICS
            snprintf( info, INFO_SIZE, "%s is skipping movement in order to follow.",
                      ptr_monam( monster, DESC_CAP_THE ) );

            mpr( info, MSGCH_DIAGNOSTICS );
#endif
            mmov_x = 0;
            mmov_y = 0;
        }

        // check for attacking another monster
        int targmon = mgrd[monster->x + mmov_x][monster->y + mmov_y];
        if (targmon != NON_MONSTER)
        {
            if (mons_aligned(monster_index(monster), targmon))
                monster_swaps_places(monster, mmov_x, mmov_y);
            else
                monsters_fight(monster_index(monster), targmon);

            // If the monster swapped places, the work's already done.
            mmov_x = 0;
            mmov_y = 0;
        }

        if (monster->type == MONS_EFREET
            || monster->type == MONS_FIRE_ELEMENTAL)
        {
            place_cloud( CLOUD_FIRE, monster->x, monster->y, 
                         2 + random2(4), monster->kill_alignment() );
        }

        if (monster->type == MONS_ROTTING_DEVIL 
                || monster->type == MONS_CURSE_TOE)
        {
            place_cloud( CLOUD_MIASMA, monster->x, monster->y, 
                         2 + random2(3), monster->kill_alignment() );
        }
    }
    else
    {
        mmov_x = mmov_y = 0;
        
        // fleeing monsters that can't move will panic and possibly
        // turn to face their attacker
        if (monster->behaviour == BEH_FLEE)
            behaviour_event(monster, ME_CORNERED);
    }

    if (mmov_x || mmov_y || (monster->confused() && one_chance_in(6)))
        do_move_monster(monster, mmov_x, mmov_y);
}                               // end monster_move()

static bool plant_spit(struct monsters *monster, struct bolt &pbolt)
{
    bool didSpit = false;

    char spit_string[INFO_SIZE];

    // setup plant spit
    pbolt.name = "acid";
    pbolt.type = SYM_ZAP;
    pbolt.range = 9;
    pbolt.rangeMax = 9;
    pbolt.colour = YELLOW;
    pbolt.flavour = BEAM_ACID;
    pbolt.beam_source = monster_index(monster);
    pbolt.damage = dice_def( 3, 7 );
    pbolt.hit = 20 + (3 * monster->hit_dice);
    pbolt.thrower = KILL_MON_MISSILE;
    pbolt.aux_source.clear();

    // fire tracer
    fire_tracer(monster, pbolt);

    if (mons_should_fire(pbolt))
    {
        strcpy( spit_string, " spits" );
        if (pbolt.target_x == you.x_pos && pbolt.target_y == you.y_pos)
            strcat( spit_string, " at you" );

        strcat( spit_string, "." );
        simple_monster_message( monster, spit_string );

        fire_beam( pbolt );
        didSpit = true;
    }

    return (didSpit);
}                               // end plant_spit()

static void mons_in_cloud(struct monsters *monster)
{
    int wc = env.cgrid[monster->x][monster->y];
    int hurted = 0;
    struct bolt beam;

    const int speed = ((monster->speed > 0) ? monster->speed : 10);
    bool wake = false;

    if (mons_is_mimic( monster->type ))
    {
        mimic_alert(monster);
        return;
    }

    switch (env.cloud[wc].type)
    {
    case CLOUD_DEBUGGING:
        end(1, false, "Fatal error: monster steps on nonexistent cloud!");
        return;

    case CLOUD_FIRE:
        if (monster->type == MONS_FIRE_VORTEX
            || monster->type == MONS_EFREET
            || monster->type == MONS_FIRE_ELEMENTAL)
        {
            return;
        }

        simple_monster_message(monster, " is engulfed in flame!");

        if (mons_res_fire(monster) > 0)
            return;

        hurted += ((random2avg(16, 3) + 6) * 10) / speed;

        if (mons_res_fire(monster) < 0)
            hurted += (random2(15) * 10) / speed;

        // remember that the above is in addition to the other you.damage.
        hurted -= random2(1 + monster->ac);
        break;                  // to damage routine at end {dlb}

    case CLOUD_STINK:
        simple_monster_message(monster, " is engulfed in noxious gasses!");

        if (mons_res_poison(monster) > 0)
            return;

        beam.flavour = BEAM_CONFUSION;

        if (1 + random2(27) >= monster->hit_dice)
            mons_ench_f2(monster, beam);

        hurted += (random2(3) * 10) / speed;
        break;                  // to damage routine at end {dlb}

    case CLOUD_COLD:
        simple_monster_message(monster, " is engulfed in freezing vapours!");

        if (mons_res_cold(monster) > 0)
            return;

        hurted += ((6 + random2avg(16, 3)) * 10) / speed;

        if (mons_res_cold(monster) < 0)
            hurted += (random2(15) * 10) / speed;

        // remember that the above is in addition to the other damage.
        hurted -= random2(1 + monster->ac);
        break;                  // to damage routine at end {dlb}

    // what of armour of poison resistance here? {dlb}
    case CLOUD_POISON:
        simple_monster_message(monster, " is engulfed in a cloud of poison!");

        if (mons_res_poison(monster) > 0)
            return;

        poison_monster(monster, env.cloud[wc].whose);
        // If the monster got poisoned, wake it up.
        wake = true;

        hurted += (random2(8) * 10) / speed;

        if (mons_res_poison(monster) < 0)
            hurted += (random2(4) * 10) / speed;
        break;                  // to damage routine at end {dlb}

    case CLOUD_STEAM:
        // couldn't be bothered coding for armour of res fire

        // what of whether it is wearing steam dragon armour? {dlb}
        if (monster->type == MONS_STEAM_DRAGON)
            return;

        simple_monster_message(monster, " is engulfed in steam!");

        if (mons_res_fire(monster) > 0)
            return;

        hurted += (random2(6) * 10) / speed;

        if (mons_res_fire(monster) < 0)
            hurted += (random2(6) * 10) / speed;

        hurted -= random2(1 + monster->ac);
        break;                  // to damage routine at end {dlb}

    case CLOUD_MIASMA:
        simple_monster_message(monster, " is engulfed in a dark miasma!");

        if (mons_holiness(monster) != MH_NATURAL 
                || monster->type == MONS_DEATH_DRAKE)
            return;

        poison_monster(monster, env.cloud[wc].whose);

        if (monster->max_hit_points > 4 && coinflip())
            monster->max_hit_points--;

        beam.flavour = BEAM_SLOW;

        if (one_chance_in(3))
            mons_ench_f2(monster, beam);

        hurted += (10 * random2avg(12, 3)) / speed;    // 3
        break;              // to damage routine at end {dlb}

    default:                // 'harmless' clouds -- colored smoke, etc {dlb}.
        return;
    }

    // A sleeping monster that sustains damage will wake up.
    if ((wake || hurted > 0) && monster->behaviour == BEH_SLEEP)
    {
        // We have no good coords to give the monster as the source of the
        // disturbance other than the cloud itself.
        behaviour_event(monster, ME_DISTURB, MHITNOT, monster->x, monster->y);
    }
    
    if (hurted < 0)
        hurted = 0;
    else if (hurted > 0)
    {
        hurt_monster(monster, hurted);

        if (monster->hit_points < 1)
        {
            mon_enchant death_ench( ENCH_NONE, 0, env.cloud[wc].whose );
            monster_die(monster, death_ench.killer(), death_ench.kill_agent());
        }
    }
}                               // end mons_in_cloud()

int monster_habitat(int which_class)
{
    switch (which_class)
    {
    case MONS_BIG_FISH:
    case MONS_GIANT_GOLDFISH:
    case MONS_ELECTRICAL_EEL:
    case MONS_JELLYFISH:
    case MONS_SWAMP_WORM:
    case MONS_WATER_ELEMENTAL:
        return (DNGN_DEEP_WATER); // no shallow water (only) monsters? {dlb}
        // must remain DEEP_WATER for now, else breaks code {dlb}

    case MONS_LAVA_WORM:
    case MONS_LAVA_FISH:
    case MONS_LAVA_SNAKE:
    case MONS_SALAMANDER:
        return (DNGN_LAVA);

    default:
        return (DNGN_FLOOR);      // closest match to terra firma {dlb}
    }
}                               // end monster_habitat()

bool monster_descriptor(int which_class, unsigned char which_descriptor)
{
    if (which_descriptor == MDSC_LEAVES_HIDE)
    {
        switch (which_class)
        {
        case MONS_DRAGON:
        case MONS_TROLL:
        case MONS_ICE_DRAGON:
        case MONS_STEAM_DRAGON:
        case MONS_MOTTLED_DRAGON:
        case MONS_STORM_DRAGON:
        case MONS_GOLDEN_DRAGON:
        case MONS_SWAMP_DRAGON:
        case MONS_YAK:
        case MONS_SHEEP:
            return (true);
        default:
            return (false);
        }
    }

    if (which_descriptor == MDSC_REGENERATES)
    {
        switch (which_class)
        {
        case MONS_CACODEMON:
        case MONS_DEEP_TROLL:
        case MONS_HELLWING:
        case MONS_IMP:
        case MONS_IRON_TROLL:
        case MONS_LEMURE:
        case MONS_ROCK_TROLL:
        case MONS_SLIME_CREATURE:
        case MONS_SNORG:
        case MONS_TROLL:
        case MONS_HYDRA:
        case MONS_KILLER_KLOWN:
            return (true);
        default:
            return (false);
        }
    }

    if (which_descriptor == MDSC_NOMSG_WOUNDS)
    {
        switch (which_class)
        {
        case MONS_RAKSHASA:
        case MONS_RAKSHASA_FAKE:
        case MONS_SKELETON_LARGE:
        case MONS_SKELETON_SMALL:
        case MONS_ZOMBIE_LARGE:
        case MONS_ZOMBIE_SMALL:
        case MONS_SIMULACRUM_SMALL:
        case MONS_SIMULACRUM_LARGE:
            return (true);
        default:
            return (false);
        }
    }
    return (false);
}                               // end monster_descriptor()

bool message_current_target()
{
    if (you.prev_targ != MHITNOT && you.prev_targ != MHITYOU)
    {
        const monsters *montarget = &menv[you.prev_targ];

        if (mons_near(montarget) && player_monster_visible(montarget))
        {
            mprf( MSGCH_PROMPT, "You are currently targeting %s "
                  "(use p/t/f to fire at it again.)",
                  ptr_monam(montarget, DESC_NOCAP_THE) );
            return (true);
        }

        mpr("You have no current target.");
    }

    return (false);
}                               // end message_current_target()

// aaah, the simple joys of pointer arithmetic! {dlb}:
unsigned int monster_index(const monsters *monster)
{
    return (monster - menv.buffer());
}                               // end monster_index()

bool hurt_monster(struct monsters * victim, int damage_dealt)
{
    bool just_a_scratch = true;

    if (damage_dealt > 0)
    {
        just_a_scratch = false;
        victim->hit_points -= damage_dealt;
    }

    return (!just_a_scratch);
}                               // end hurt_monster()

bool heal_monster(struct monsters * patient, int health_boost,
                  bool permit_growth)
{
    if (mons_is_statue(patient->type))
        return (false);

    if (health_boost < 1)
        return (false);
    else if (!permit_growth && patient->hit_points == patient->max_hit_points)
        return (false);
    else
    {
        patient->hit_points += health_boost; 

        if (patient->hit_points > patient->max_hit_points)
        {
            if (permit_growth)
                patient->max_hit_points++;

            patient->hit_points = patient->max_hit_points;
        }
    }

    return (true);
}                               // end heal_monster()

static int map_wand_to_mspell(int wand_type)
{
    int mzap = 0;

    switch (wand_type)
    {
        case WAND_FLAME:
            mzap = MS_FLAME;
            break;
        case WAND_FROST:
            mzap = MS_FROST;
            break;
        case WAND_SLOWING:
            mzap = MS_SLOW;
            break;
        case WAND_HASTING:
            mzap = MS_HASTE;
            break;
        case WAND_MAGIC_DARTS:
            mzap = MS_MMISSILE;
            break;
        case WAND_HEALING:
            mzap = MS_HEAL;
            break;
        case WAND_PARALYSIS:
            mzap = MS_PARALYSIS;
            break;
        case WAND_FIRE:
            mzap = MS_FIRE_BOLT;
            break;
        case WAND_COLD:
            mzap = MS_COLD_BOLT;
            break;
        case WAND_CONFUSION:
            mzap = MS_CONFUSE;
            break;
        case WAND_INVISIBILITY:
            mzap = MS_INVIS;
            break;
        case WAND_TELEPORTATION:
            mzap = MS_TELEPORT_OTHER;
            break;
        case WAND_LIGHTNING:
            mzap = MS_LIGHTNING_BOLT;
            break;
        case WAND_DRAINING:
            mzap = MS_NEGATIVE_BOLT;
            break;
        default:
            mzap = 0;
            break;
    }

    return (mzap);
}

void seen_monster(monsters *monster)
{
    if ( monster->flags & MF_SEEN )
        return;
    
    // First time we've seen this particular monster
    monster->flags |= MF_SEEN;
    
    if ( !mons_is_mimic(monster->type)
         && MONST_INTERESTING(monster)
         && monster->type != MONS_PANDEMONIUM_DEMON
         && monster->type != MONS_PLAYER_GHOST )
    {
        take_note(
            Note(NOTE_SEEN_MONSTER, monster->type, 0,
                 ptr_monam(monster, DESC_NOCAP_A)) );
    }
}

//---------------------------------------------------------------
//
// shift_monster
//
// Moves a monster to approximately (x,y) and returns true 
// if monster was moved.
//
//---------------------------------------------------------------
bool shift_monster( struct monsters *mon, int x, int y )
{
    bool found_move = false;

    int i, j;
    int tx, ty;
    int nx = 0, ny = 0;

    int count = 0;

    if (x == 0 && y == 0)
    {
        // try and find a random floor space some distance away
        for (i = 0; i < 50; i++)
        {
            tx = 5 + random2( GXM - 10 );
            ty = 5 + random2( GYM - 10 );

            int dist = grid_distance(x, y, tx, ty);
            if (grd[tx][ty] == DNGN_FLOOR && dist > 10)
                break;
        }

        if (i == 50)
            return (false);
    }

    for (i = -1; i <= 1; i++)
    {
        for (j = -1; j <= 1; j++)
        {
            tx = x + i;
            ty = y + j;

            if (tx < 5 || tx > GXM - 5 || ty < 5 || ty > GXM - 5)
                continue;

            // won't drop on anything but vanilla floor right now
            if (grd[tx][ty] != DNGN_FLOOR)
                continue;

            if (mgrd[tx][ty] != NON_MONSTER)
                continue;

            if (tx == you.x_pos && ty == you.y_pos)
                continue;

            count++;
            if (one_chance_in(count))
            {
                nx = tx;
                ny = ty;
                found_move = true;
            }
        }
    }

    if (found_move)
    {
        const int mon_index = mgrd[mon->x][mon->y];
        mgrd[mon->x][mon->y] = NON_MONSTER;
        mgrd[nx][ny] = mon_index;
        mon->x = nx;
        mon->y = ny;
    }

    return (found_move);
}
