#include "Board.h"
#include "Console.h"
#include "Dvar.h"
#include "Globals.h"
#include "Matrix.h"
#include "Quad.h"
#include "Shader.h"
#include "String.h"
#include <GL/glew.h>
#include <stdlib.h>
#include <string.h>

Texture tex_blocks;
unsigned short tex_blocks_divx, tex_blocks_divy;

typedef struct {
	char block_id;
	short index;
} TextureBinding;

typedef struct TextureLevel_s {
	TextureBinding *texdata;
	unsigned int texdata_size;

	struct TextureLevel_s *next_level;
} TextureLevel;

TextureLevel *current_level;

void BoardUseNextBlock(Board *board) {
	BlockSetRandom(&board->block, board->rows - 1);
}

void BoardCreate(Board *board) {
	byte *data_block = (byte*)malloc(board->rows * board->columns);

	board->data = (byte**)malloc(board->rows * sizeof(byte*));
	for (int i = 0; i < board->rows; ++i)
		board->data[i] = data_block + (i * board->columns);

	QuadCreate(&board->quad_blocks);
}

void BoardFree(Board *board) {
	QuadDelete(&board->quad_blocks);

	free(&board->data[0][0]);
	free(board->data);
	free(board->block.data);
}

void BoardClear(Board *board) {
	for (byte i = 0; i < board->rows; ++i)
		ZeroMemory(board->data[i], board->columns);
}

void BoardSetIDSize(Board *board, float id_size) {
	QuadSetData(&board->quad_blocks, id_size / (float)tex_blocks.width, id_size / (float)tex_blocks.height);

	tex_blocks_divx = tex_blocks.width / (unsigned short)id_size;
	tex_blocks_divy = tex_blocks.height / (unsigned short)id_size;
}

inline void ClearRow(Board *board, unsigned int row) {
	for (unsigned short r = row; r < board->rows - 1; ++r)
		for (unsigned short c = 0; c < board->columns; ++c)
			board->data[r][c] = board->data[r + 1][c];

	for (unsigned short c = 0; c < board->columns; ++c)
		board->data[board->rows - 1][c] = 0;
}

inline bool RowIsFull(Board *board, unsigned int row) {
	for (unsigned int c = 0; c < board->columns; ++c)
		if (board->data[row][c] == 0)
			return false;

	return true;
}

void boardCheckForClears(Board *board) {
	for (unsigned int r = 0; r < board->rows;)
		if (RowIsFull(board, r))
			ClearRow(board, r);
		else
			++r;
}

void BoardSubmitBlock(Board *board) {
	for (unsigned int r = 0; r < board->block.size; ++r)
		for (unsigned int c = 0; c < board->block.size; ++c)
			if (board->block.data[RC1D(board->block.size, r, c)])
				board->data[board->block.y + r][board->block.x + c] = board->block.data[RC1D(board->block.size, r, c)];

	boardCheckForClears(board);
}

bool BoardCheckMove(const Board *board, short x, short y) {
	unsigned int size = board->block.size;
	unsigned int sizesq = size * size;;

	short r, c;

	for (unsigned int i = 0; i < sizesq; ++i) {
		if (board->block.data[i]) {
			r = board->block.y + y + (i / size);
			c = board->block.x + x + (i % size);

			if (r < 0 || c < 0 || r >= board->rows || c >= board->columns || board->data[r][c])
				return false;
		}
	}

	return true;
}

short TextureLevelIDIndex(char id) {
	for (unsigned int i = 0; i < current_level->texdata_size; ++i)
		if (current_level->texdata[i].block_id == id)
			return current_level->texdata[i].index;

	return -1;
}

void BoardRender(const Board *board) {
	Mat3 transform;
	
	glBindTexture(GL_TEXTURE_2D, 0);
	Mat3Identity(transform);
	Mat3Scale(transform, board->width, board->height);
	Mat3Translate(transform, board->x, board->y);
	ShaderSetUniformMat3(g_active_shader, "u_transform", transform);
	QuadRender(&board->quad_blocks);

	glBindTexture(GL_TEXTURE_2D, tex_blocks.glid);

	float block_w = (float)((float)board->width / (float)board->columns);
	float block_h = (float)((float)board->height / (float)board->rows);

	float uvoffset[2] = {0.f, 0.001f / tex_blocks.height}; //stupid but it works

	Mat3Identity(transform);
	Mat3Scale(transform, block_w, block_h);
	Mat3Translate(transform, board->x, board->y);

	RenderTileBuffer(&board->data[0][0], board->rows, board->columns, tex_blocks_divx, tex_blocks_divy, transform, &board->quad_blocks, uvoffset);

	Mat3Identity(transform);
	Mat3Scale(transform, block_w, block_h);
	Mat3Translate(transform, board->x + (board->block.x * block_w), board->y + (board->block.y * block_h));

	RenderTileBuffer(board->block.data, board->block.size, board->block.size, tex_blocks_divx, tex_blocks_divy, transform, &board->quad_blocks, uvoffset);
}

//Input

bool BoardInputDown(Board *board) {
	if (BoardCheckMove(board, 0, -1)) {
		--board->block.y;
		return true;
	}

	BoardSubmitBlock(board);
	BoardUseNextBlock(board);

	return false;
}

bool BoardInputX(Board *board, int x) {
	if (BoardCheckMove(board, x, 0)) {
		board->block.x += x;
		return true;
	}

	return false;
}

bool BoardInputCCW(Board *board) {
	BlockRotateCCW(&board->block);
	
	if (!BoardCheckMove(board, 0, 0)) {
		BlockRotateCW(&board->block);
		return false;
	}

	return true;
}
bool BoardInputCW(Board *board) {
	BlockRotateCW(&board->block);

	if (!BoardCheckMove(board, 0, 0)) {
		BlockRotateCCW(&board->block);
		return false;
	}

	return true;
}


////

char **id_groups;
unsigned int group_count = 0;
unsigned int blockid_count;

TextureLevel *first_level;

void UseNextTextureLevel() {
	current_level = current_level->next_level;
	
	if (!current_level) current_level = first_level;
}

void CLSetTextureIndexOrder(const char **tokens, unsigned int count) {
	FreeTokens(id_groups, group_count);

	id_groups = (char**)malloc(count * sizeof(char*));

	blockid_count = 0;
	for (unsigned int i = 0; i < count; ++i) {
		id_groups[i] = DupString(tokens[i]);
		blockid_count += (unsigned int)strlen(tokens[i]);
	}

	group_count = count;
}

void CLAddTextureLevel(const char **tokens, unsigned int count) {
	if (count != group_count) {
		ConsolePrint("Error : Invalid argument count\n");
		return;
	}

	TextureLevel *new_level = (TextureLevel*)malloc(sizeof(TextureLevel));
	new_level->next_level = NULL;
	new_level->texdata = (TextureBinding*)malloc(blockid_count * sizeof(TextureBinding));
	new_level->texdata_size = blockid_count;

	unsigned int counter = 0;
	for (unsigned int i = 0; i < group_count; ++i) {
		short texid = (short)atoi(tokens[i]);
		for (const char *c = id_groups[i]; *c != '\0'; ++c) {
			new_level->texdata[counter].block_id = *c;
			new_level->texdata[counter].index = texid;
			++counter;
		}
	}

	if (!first_level) {
		first_level = new_level;
		current_level = first_level;
	}
	else {
		TextureLevel *level = first_level;
		for (; level->next_level; level = level->next_level);

		level->next_level = new_level;
	}
}

void ClearTextureLevels() {
	TextureLevel *next;
	
	while (first_level) {
		next = first_level->next_level;
		free(first_level->texdata);
		free(first_level);
		first_level = next;
	}
}

void C_CLBlockTexture(DvarValue string) {
	TextureFromFile(string.string, &tex_blocks);
}
