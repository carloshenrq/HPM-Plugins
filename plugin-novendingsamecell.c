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
#include "common/utils.h"
#include "map/pc.h"
#include "map/map.h"
#include "map/chat.h"
#include "map/vending.h"

#include "plugins/HPMHooking.h"
#include "common/HPMDataCheck.h"/* should always be the last file included! (if you don't make it last, it'll intentionally break compile time) */

HPExport struct hplugin_info pinfo = {
	"plugin-novendingsamecell",
	SERVER_TYPE_MAP,
	"0.1",
	HPM_VERSION,
};

/**
 * Configuração para definir o range de criação de lojas que não pode
 * ser um ao lado do outro.
 * min: 0
 * max: 5
 */
int config_vending_create_range = 1;

/**
 * Configuração para definir o range de criação de lojinhas próximas a NPCs
 *
 * min: 0
 * max: 5
 */
int config_vending_create_npc_range = 3;

// Variavel para segurar informações de venda
// das rotinas anteriores.
void (*vending_open) (struct map_session_data *sd, const char *message, const uint8 *data, int count);
bool (*chat_createchat) (struct map_session_data *sd, const char *title, const char *pass, int limit, bool pub);

/**
 * Verifica quantos jogadores estão com vendinha aberta dentro
 * da celula informada.
 */
static int vending_search_pc_sub(struct block_list* bl, va_list va)
{
	int id = va_arg(va, int);
	struct map_session_data* sd = NULL;

	// Se o BLOCK não for do tipo PC ou for igual ao id
	// informado...
	if(bl->type != BL_PC || bl->id == id)
		return 0;

	// Se não encontrar o jogador... então transforma
	// o block em session de jogador	
	if((sd = BL_CAST(BL_PC, bl)) == NULL)
		return 0;

	// Se o usuário estiver vendendo ou possuir um
	// chat aberto, então, ele conta
	return (sd->state.vending || sd->chat_id);
}

/**
 * Procura npcs próximos ao jogador que está abrindo a lojinha.
 */
static int vending_search_npc_sub(struct block_list* bl, va_list va)
{
	// Se houver o BLOCK_LIST e for do tipo npc,
	// retorna 1 para a contagem.
	return bl && bl->type == BL_NPC ? 1 : 0;
}

/**
 * Define nova rotina para verificação de abertura de lojinhas. 
 */
static void vending_openvending(struct map_session_data *sd, const char *message, const uint8 *data, int count)
{
	// Faz a contagem de quantos jogadores possuem lojas abertas dentro da
	// celula informada.
	int vending_count = 0;

	nullpo_retv(sd);

	// Verifica configuração para testar range de NPCs próximos.
	if(config_vending_create_npc_range)
	{
		// Verifica quantos NPCs existe algum npc dentro de um range de 3 celulas do jogador.
		// Se existir, o jogador não poderá abrir uma lojinha.
		vending_count = map->foreachinrange(vending_search_npc_sub, &sd->bl, config_vending_create_npc_range, BL_NPC);
		if(vending_count > 0)
		{
			clif->messagecolor_self(sd->fd, COLOR_RED, "[Servidor] Você está muito perto de um NPC para abrir sua lojinha.");
			return;
		}
	}

	// Verifica se há jogadores na celula do jogador. Se houver, então não permite
	// a abertura da lojinha.
	vending_count = map->foreachincell(vending_search_pc_sub, sd->bl.m, sd->bl.x, sd->bl.y, BL_PC, sd->bl.id);
	if(vending_count > 0)
	{
		clif->messagecolor_self(sd->fd, COLOR_RED, "[Servidor] Já existe um chat/loja neste lugar.");
		return;
	}

	if(config_vending_create_range)
	{
		// Procura vendas a um raio de 1 celula ao redor do jogador.
		// A Loja não poderá ser acerta, caso exista qualquer loja ali por perto.
		vending_count = map->foreachinrange(vending_search_pc_sub, &sd->bl, config_vending_create_range, BL_PC, sd->bl.id);
		if(vending_count > 0)
		{
			clif->messagecolor_self(sd->fd, COLOR_RED, "[Servidor] Você está muito próximo de um chat/loja para abrir sua lojinha.");
			return;
		}
	}

	// Tenta fazer a abertura da lojinha por vias normais.
	// Chama a rotina comum para a abertura da lojinha
	vending_open(sd, message, data, count);
}

static bool chat_createpcchat(struct map_session_data *sd, const char *title, const char *pass, int limit, bool pub)
{
	// Faz a contagem de quantos jogadores possuem lojas abertas dentro da
	// celula informada.
	int vending_count = 0;

	// Retorna false se não conseguir verificar os titulos
	// senhas e etc...
	nullpo_ret(sd);
	nullpo_ret(title);
	nullpo_ret(pass);

	// Se estiver configurado para verificar npcs próximos...
	if(config_vending_create_npc_range)
	{
		// Verifica quantos NPCs existe algum npc dentro de um range de 3 celulas do jogador.
		// Se existir, o jogador não poderá abrir uma lojinha.
		vending_count = map->foreachinrange(vending_search_npc_sub, &sd->bl, config_vending_create_npc_range, BL_NPC);
		if(vending_count > 0)
		{
			clif->messagecolor_self(sd->fd, COLOR_RED, "[Servidor] Você está muito perto de um NPC para abrir sua lojinha.");
			return false;
		}
	}

	// Verifica se há jogadores na celula do jogador. Se houver, então não permite
	// a abertura da lojinha.
	vending_count = map->foreachincell(vending_search_pc_sub, sd->bl.m, sd->bl.x, sd->bl.y, BL_PC, sd->bl.id);
	if(vending_count > 0)
	{
		clif->messagecolor_self(sd->fd, COLOR_RED, "[Servidor] Já existe um chat/loja neste lugar.");
		return false;
	}

	// Se estiver configurado para bloquear players ao redor um do outro...
	if(config_vending_create_range)
	{
		// Procura vendas a um raio de 1 celula ao redor do jogador.
		// A Loja não poderá ser acerta, caso exista qualquer loja ali por perto.
		vending_count = map->foreachinrange(vending_search_pc_sub, &sd->bl, config_vending_create_range, BL_PC, sd->bl.id);
		if(vending_count > 0)
		{
			clif->messagecolor_self(sd->fd, COLOR_RED, "[Servidor] Você está muito próximo de um chat/loja para abrir sua lojinha.");
			return false;
		}
	}

	// Chama a rotina default para criação do chat.
	return chat_createchat(sd, title, pass, limit, pub);
}

/**
 * Define informações de configuração
 *
 * @param key Chave a ser definida
 * @param value Valor da chave.
 */
static void battle_config_set(const char* key, const char* value)
{
	if(!strcmpi(key, "battle_configuration/create_vend_range"))
		config_vending_create_range = cap_value(atoi(value), 0, 5);
	else if(!strcmpi(key, "battle_configuration/create_npc_range"))
		config_vending_create_npc_range = cap_value(atoi(value), 0, 5);

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
	if(!strcmpi(key, "battle_configuration/create_vend_range"))
		return config_vending_create_range;
	else if(!strcmpi(key, "battle_configuration/create_npc_range"))
		return config_vending_create_npc_range;

	return 0;
}

/**
 * Inicializador para hooks das rotinas.
 */
HPExport void server_preinit(void)
{
	// Adicionado configuração para leitura dos ranges de criação
	// dos chats e lojas.
	addBattleConf("battle_configuration/create_vend_range", battle_config_set, battle_config_get, false);
	addBattleConf("battle_configuration/create_npc_range", battle_config_set, battle_config_get, false);

	// Criação de lojas de usuário.
	vending_open = vending->open;
	vending->open = vending_openvending;

	// Criação de chats de usuário.
	chat_createchat = chat->create_pc_chat;
	chat->create_pc_chat = chat_createpcchat;
}

HPExport void server_online(void)
{
	ShowInfo("'%s' Plugin por CarlosHenrq [brAthena]. Versão '%s'\n", pinfo.name, pinfo.version);
}
