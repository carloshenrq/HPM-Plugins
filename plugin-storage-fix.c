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
#include "common/socket.h"
#include "common/utils.h"
#include "map/party.h"
#include "map/map.h"
#include "map/mob.h"
#include "map/pc.h"
#include "map/storage.h"
#include "map/battle.h"

#include "plugins/HPMHooking.h"
#include "common/HPMDataCheck.h"/* should always be the last file included! (if you don't make it last, it'll intentionally break compile time) */

HPExport struct hplugin_info pinfo = {
	"plugin-storage-fix",
	SERVER_TYPE_MAP,
	"0.1",
	HPM_VERSION,
};

/**
 * Verifica se as adições extras de storage acumulativo foram resolvidos.
 * @var boolean Se estiver verdadeiro é porque foi ativado.
 */
bool battle_config_storage_fix_enable = false;

/**
 * Define informações de configuração
 *
 * @param key Chave a ser definida
 * @param value Valor da chave.
 */
static void battle_config_set(const char* key, const char* value)
{
	if(!strcmpi(key, "battle_configuration/storage_new_type"))
	{
		battle_config_storage_fix_enable = (bool)cap_value(atoi(value), 0, 1);
	}

	return;
}

/**
 * Verifica informações de definição para batalha.
 *
 * @param key Chave a ser verificada.
 *
 * @return int Valor para a configuração.
 */
static int battle_config_get(const char* key)
{
	if(!strcmpi(key, "battle_configuration/storage_new_type"))
		return (int)battle_config_storage_fix_enable;

	return 0;
}

/* helper function
 * checking if 2 item structure are identique
 */
static int compare_item(struct item *a, struct item *b)
{
	if( a->nameid == b->nameid &&
		a->identify == b->identify &&
		a->refine == b->refine &&
		a->attribute == b->attribute &&
		a->expire_time == b->expire_time &&
		a->bound == b->bound &&
		a->unique_id == b->unique_id)
	{
		int i = 0, k = 0;
		ARR_FIND(0, MAX_SLOTS, i, a->card[i] != b->card[i]);
		ARR_FIND(0, MAX_ITEM_OPTIONS, k, a->option[k].index != b->option[k].index || a->option[k].value != b->option[k].value);

		if (i == MAX_SLOTS && k == MAX_ITEM_OPTIONS)
			return 1;
	}
	return 0;
}

/**
 * Rotinas que adicionam o item ao storage do jogador.
 */
int (*storage_additem) (struct map_session_data *sd, struct item *item_data, int amount);
static int STRG_storage_additem(struct map_session_data *sd, struct item *item_data, int amount)
{
	// Se a configuração estiver habilitada para adicionar os itens de storage
	// conforme as informações dadas 
	if(battle_config_storage_fix_enable && amount > 0)
	{
		struct item_data *data = NULL;
		struct item *it = NULL;
		int i, total_inicial = amount;
		bool used_storage = false;

		nullpo_retr(1, sd);
		Assert_retr(1, sd->storage.received == true);

		nullpo_retr(1, item_data);
		Assert_retr(1, item_data->nameid > 0);

		Assert_retr(1, amount > 0);

		data = itemdb->search(item_data->nameid);

		if (data->stack.storage && amount > data->stack.amount) // item stack limitation
			return 1;

		if (!itemdb_canstore(item_data, pc_get_group_level(sd))) {
			//Check if item is storable. [Skotlex]
			clif->message (sd->fd, msg_sd(sd, 264)); // This item cannot be stored.
			return 1;
		}

		if (item_data->bound > IBT_ACCOUNT && !pc_can_give_bound_items(sd)) {
			clif->message(sd->fd, msg_sd(sd, 294)); // This bound item cannot be stored there.
			return 1;
		}

		if (itemdb->isstackable2(data))
		{
			for (i = 0; i < VECTOR_LENGTH(sd->storage.item); i++) {
				it = &VECTOR_INDEX(sd->storage.item, i);

				if (it->nameid == 0)
					continue;

				if (compare_item(it, item_data))
				{ // existing items found, stack them

					// Quantidade de itens que podem ser armazenados por
					// item do tipo stackable
					int MAX_STACK = data->stack.storage ? data->stack.amount : MAX_AMOUNT;
					// Quantidade de itens que ainda podem ser colocados
					int DIFF_STACK = 0, AMOUNT_PUT = 0;

					// Busca o próximo item
					if(it->amount >= MAX_STACK)
						continue;

					// Quantos itens ainda podem ser colocados neste registro.
					DIFF_STACK = MAX_STACK - it->amount;

					// Quantos realmente serão colocados?
					AMOUNT_PUT = cap_value(amount, 1, DIFF_STACK);

					// Soma a quantidade que foi adicionada
					// ao storage e logo após, atualiza o pacote.
					it->amount += AMOUNT_PUT;
					clif->storageitemadded(sd, it, i, AMOUNT_PUT);
					sd->storage.save = true; // set a save flag.

					// Retira o total a colocar no storage do que realmente será colocado.
					amount -= AMOUNT_PUT;

					// Se não houver mais nada a colocar
					// então, retorna 0
					if(!amount)
						return 0;

					// Se usou os dados de armazem...
					used_storage = true;
				}
			}

			/**
			 * Caso chegue a este ponto é para criar uma nova entrada no banco de dados
			 * referente ao item.
			 */

			// Check if storage exceeds limit.
			if (sd->storage.aggregate >= MAX_STORAGE)
			{
				// Se usou o storage e atualizou o total
				// do banco de dados...
				// Atualiza informações do inventário e também
				// do carrinho.
				if(used_storage && total_inicial != amount)
				{
					item_data->amount -= (total_inicial - amount);

					// Atualiza carrinho e inventário
					clif->inventorylist(sd);
					if(pc_iscarton(sd))
					{
						clif->cartlist(sd);
						clif->updatestatus(sd, SP_CARTINFO);
					}
				}

				return 1;
			}

			ARR_FIND(0, VECTOR_LENGTH(sd->storage.item), i, VECTOR_INDEX(sd->storage.item, i).nameid == 0);

			if (i == VECTOR_LENGTH(sd->storage.item)) {
				VECTOR_ENSURE(sd->storage.item, 1, 1);
				VECTOR_PUSH(sd->storage.item, *item_data);
				it = &VECTOR_LAST(sd->storage.item);
			} else {
				it = &VECTOR_INDEX(sd->storage.item, i);
				*it = *item_data;
			}

			it->amount = amount;

			sd->storage.aggregate++;
			clif->storageitemadded(sd, it, i, amount);
			clif->updatestorageamount(sd, sd->storage.aggregate, MAX_STORAGE);
			sd->storage.save = true; // set a save flag.

			return 0;
		}
	}

	return storage_additem(sd, item_data, amount);
}

/**
 * Inicializador para o plugin
 */
HPExport void plugin_init (void)
{
	// Adicionado plugins de inicio dados de inicialização do plugin
	storage_additem = *storage->additem;
	storage->additem = STRG_storage_additem;
}

HPExport void server_preinit(void)
{
	addBattleConf("battle_configuration/storage_new_type", battle_config_set, battle_config_get, true);
}

HPExport void server_online(void)
{
	ShowInfo("'%s' Plugin por CarlosHenrq [brAthena]. Versão '%s'\n", pinfo.name, pinfo.version);
}
