/*
Copyright (C) 2018  CarlosHenrq

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common/hercules.h" /* Should always be the first Hercules file included! (if you don't make it first, you won't be able to use interfaces) */
#include "common/HPMi.h"
#include "common/timer.h"
#include "common/nullpo.h"
#include "common/cbasetypes.h"
#include "common/memmgr.h"
#include "map/pc.h"
#include "map/map.h"
#include "map/guild.h"
#include "map/clif.h"

#include "plugins/HPMHooking.h"
#include "common/HPMDataCheck.h"/* should always be the last file included! (if you don't make it last, it'll intentionally break compile time) */

HPExport struct hplugin_info pinfo = {
	"plugin-treasure-protect",
	SERVER_TYPE_MAP,
	"0.1",
	HPM_VERSION,
};

/**
 * Adicionado proteção de dano ao aos baus de castelo
 * somente o lider da guild do dono do castelo poderá acertar o baú.
 */
static int64 POST_battle_calc_gvg_damage(int64 ret, struct block_list *src, struct block_list *bl, int64 damage, int div_, uint16 skill_id, uint16 skill_lv, int flag)
{
	/**
	 * Se for um monstro, num mapa GVG de castelo
	 * entrará no teste para saber se poderá ser acertado o damage.
	 */
	if(bl->type == BL_MOB && map->list[bl->m].flag.gvg_castle)
	{
		// Obtém o código do monstro a ser verificado...
		int class_ = status->get_class(bl);

		// Baús de castelo da WoE 1.0
		// Baús de castelo da WoE 2.0
		if((class_ >= 1324 && class_ <= 1363) || (class_ >= 1938 && class_ <= 1946))
		{
			struct guild_castle* gc = guild->mapindex2gc(map_id2index(bl->m));

			if(gc != NULL)
			{
				struct guild* g = NULL;
				struct map_session_data* sd = BL_UCAST(BL_PC, src);

				// Verifica informações do código...
				int src_guild_id = status->get_guild_id(src);

				// Guild é diferente do código informado...
				if(gc->guild_id != src_guild_id || src->type != BL_PC)
					return (ret = (int64)0);

				// Obtém informações sobre a guild dona do castelo...
				if((g = guild->search(gc->guild_id)) == NULL || (sd = BL_UCAST(BL_PC, src)) == NULL)
					return (ret = (int64)0);

				// Verifica se o lider da guild é o mesmo personagem
				// que está atacando, se não for, então
				// Será negado o dano...
				if(sd->status.char_id != g->member[0].char_id)
					return (ret = (int64)0);
			}
		}
	}

	// Retorna o dano informado...
	return ret;
}

/**
 * Inicializador para o plugin
 */
HPExport void plugin_init (void)
{
	addHookPost(battle, calc_gvg_damage, POST_battle_calc_gvg_damage);
}

HPExport void server_online(void)
{
	ShowInfo("'%s' Plugin por CarlosHenrq[brAthena]. Versão '%s'\n", pinfo.name, pinfo.version);
}
