#include "ihex.h"
#include <stdbool.h>
#include <string.h>

//****************************************************************************************************************
// Local Defines
//****************************************************************************************************************

#define NUM_BYTE_COUNT_DIGITS	2
#define NUM_ADDRESS_DIGITS		4
#define NUM_TYPE_DIGITS			2
#define NUM_CHECK_SUM_DIGITS	2


//****************************************************************************************************************
// Local Function-Prototypes
//****************************************************************************************************************

static ihex_tMessage ihex_Lexer(ihex_tReader* reader, uint8_t data);
static void ihex_SetNextState(ihex_tReader* reader, ihex_tLexerState next_state, uint16_t num_expected_digits);
static uint16_t ihex_InsertDigit(uint16_t src_val, uint8_t nibble_data, uint8_t digit_pos);
static ihex_tMessage ihex_LexerTokenStream(ihex_tReader* reader, ihex_tLexerTokenType token_type, uint16_t token_data);
static bool ihex_IsValidData(uint8_t data);
static int8_t ihex_CharToValue(uint8_t data);


//****************************************************************************************************************
// Global Functions
//****************************************************************************************************************

void ihex_Init(ihex_tReader* reader, ihex_tDataCallback func_data)
{
	reader->funcData = func_data;
}

void ihex_Begin(ihex_tReader* reader)
{
	reader->lexer.state = IHEX_LEXER_STATE_WAIT_COLON;
	reader->lexer.ext_offset = 0x00000000;
}

ihex_tMessage ihex_Put(ihex_tReader* reader, uint8_t data)
{
	uint8_t digit_pos;
	int8_t temp_digit = 0; //single parsed digit

	if (data == '\n')
		return IHEX_MESSAGE_CONTINUE;

	if (data == '\r')
		return IHEX_MESSAGE_CONTINUE;

	if (!ihex_IsValidData(data))
		return IHEX_MESSAGE_INVALID_INPUT_DATA;

	return ihex_Lexer(reader, data);
}


//****************************************************************************************************************
// Local Functions
//****************************************************************************************************************

static ihex_tMessage ihex_Lexer(ihex_tReader* reader, uint8_t data)
{
	ihex_tMessage message = IHEX_MESSAGE_CONTINUE;
	uint8_t digit_pos;
	int8_t temp_digit = 0;

	switch (reader->lexer.state)
	{
	case IHEX_LEXER_STATE_WAIT_COLON:
		if (data != ':')
			break;

		memset(&reader->lexer.record_data, 0, sizeof(ihex_tRecordData));
		reader->lexer.record_data.record_mark = data;
		message = ihex_LexerTokenStream(reader, IHEX_LEXER_TOKENTYPE_RECORD_MARK, reader->lexer.record_data.record_mark);

		ihex_SetNextState(reader, IHEX_LEXER_STATE_WAIT_BYTE_COUNT, NUM_BYTE_COUNT_DIGITS);
		break;

	case IHEX_LEXER_STATE_WAIT_BYTE_COUNT:
		reader->lexer.num_expected_characters--;

		temp_digit = ihex_CharToValue(data);

		if (temp_digit < 0)
		{
			message = IHEX_MESSAGE_INVALID_INPUT_DATA;
			break;
		}

		reader->lexer.record_data.reclen = ihex_InsertDigit(reader->lexer.record_data.reclen, temp_digit, reader->lexer.num_expected_characters);

		if (reader->lexer.num_expected_characters == 0)
		{
			message = ihex_LexerTokenStream(reader, IHEX_LEXER_TOKENTYPE_RECLEN, reader->lexer.record_data.reclen);
			ihex_SetNextState(reader, IHEX_LEXER_STATE_WAIT_ADDRESS, NUM_ADDRESS_DIGITS);
		}
		break;

	case IHEX_LEXER_STATE_WAIT_ADDRESS:
		reader->lexer.num_expected_characters--;
		temp_digit = ihex_CharToValue(data);

		if (temp_digit < 0)
		{
			message = IHEX_MESSAGE_INVALID_INPUT_DATA;
			break;
		}

		reader->lexer.record_data.load_offset = ihex_InsertDigit(reader->lexer.record_data.load_offset, temp_digit, reader->lexer.num_expected_characters);

		if (reader->lexer.num_expected_characters == 0)
		{
			message = ihex_LexerTokenStream(reader, IHEX_LEXER_TOKENTYPE_LOAD_OFFSET, reader->lexer.record_data.load_offset);
			ihex_SetNextState(reader, IHEX_LEXER_STATE_WAIT_TYPE, NUM_TYPE_DIGITS);
		}

		break;

	case IHEX_LEXER_STATE_WAIT_TYPE:
		reader->lexer.num_expected_characters--;

		temp_digit = ihex_CharToValue(data);

		if (temp_digit < 0)
		{
			message = IHEX_MESSAGE_INVALID_INPUT_DATA;
			break;
		}

		reader->lexer.record_data.rectyp = ihex_InsertDigit(reader->lexer.record_data.rectyp, temp_digit, reader->lexer.num_expected_characters);

		if (reader->lexer.num_expected_characters == 0)
		{
			message = ihex_LexerTokenStream(reader, IHEX_LEXER_TOKENTYPE_RECTYP, reader->lexer.record_data.rectyp);
			reader->lexer.record_data.data = 0x00;

			if (reader->lexer.record_data.reclen > 0)
				ihex_SetNextState(reader, IHEX_LEXER_STATE_WAIT_DATA, reader->lexer.record_data.reclen * 2);
			else
				ihex_SetNextState(reader, IHEX_LEXER_STATE_WAIT_CHECK_SUM, NUM_CHECK_SUM_DIGITS);
		}

		break;

	case IHEX_LEXER_STATE_WAIT_DATA:
		reader->lexer.num_expected_characters--;

		temp_digit = ihex_CharToValue(data);

		if (temp_digit < 0)
		{
			message = IHEX_MESSAGE_INVALID_INPUT_DATA;
			break;
		}


		digit_pos = reader->lexer.num_expected_characters % 2;
		reader->lexer.record_data.data = ihex_InsertDigit(reader->lexer.record_data.data, temp_digit, digit_pos);

		if (digit_pos == 0)
		{
			message = ihex_LexerTokenStream(reader, IHEX_LEXER_TOKENTYPE_DATA, reader->lexer.record_data.data);
			reader->lexer.record_data.data = 0x00;
		}

		if (reader->lexer.num_expected_characters == 0)
			ihex_SetNextState(reader, IHEX_LEXER_STATE_WAIT_CHECK_SUM, NUM_CHECK_SUM_DIGITS);

		break;

	case IHEX_LEXER_STATE_WAIT_CHECK_SUM:
		reader->lexer.num_expected_characters--;

		temp_digit = ihex_CharToValue(data);

		if (temp_digit < 0)
		{
			message = IHEX_MESSAGE_INVALID_INPUT_DATA;
			break;
		}

		reader->lexer.record_data.target_check_sum = ihex_InsertDigit(reader->lexer.record_data.target_check_sum, temp_digit, reader->lexer.num_expected_characters);

		if (reader->lexer.num_expected_characters == 0)
		{

			message = ihex_LexerTokenStream(reader, IHEX_LEXER_TOKENTYPE_CHKSUM, reader->lexer.record_data.target_check_sum);
			reader->lexer.state = IHEX_LEXER_STATE_WAIT_COLON;
		}
		break;
	}

	return message;
}



static void ihex_SetNextState(ihex_tReader* reader, ihex_tLexerState next_state, uint16_t num_expected_digits)
{
	reader->lexer.state = next_state;
	reader->lexer.num_expected_characters = num_expected_digits;
}

static uint16_t ihex_InsertDigit(uint16_t src_val, uint8_t nibble_data, uint8_t digit_pos)
{
	uint16_t dst_val;
	uint16_t mask = 0x000F;
	uint16_t new_data = (uint16_t)(nibble_data & 0x0F) << (digit_pos * 4);

	mask <<= (digit_pos * 4);
	dst_val = src_val & ~mask;
	dst_val |= new_data & mask;
	return dst_val;
}



static ihex_tMessage ihex_LexerTokenStream(ihex_tReader* reader, ihex_tLexerTokenType token_type, uint16_t token_data)
{
	ihex_tMessage message = IHEX_MESSAGE_CONTINUE;
	
	switch (token_type)
	{
	case IHEX_LEXER_TOKENTYPE_RECORD_MARK:
		reader->lexer.calc_chksum = 0;
		reader->lexer.rec_byte_offset = 0x0000;
		break;

	case IHEX_LEXER_TOKENTYPE_RECLEN:
		reader->lexer.calc_chksum += token_data;
		break;

	case IHEX_LEXER_TOKENTYPE_LOAD_OFFSET:
		reader->lexer.calc_chksum += (token_data >> 8) + (token_data & 0xFF);
		break;

	case IHEX_LEXER_TOKENTYPE_RECTYP:
		reader->lexer.calc_chksum += token_data;

		if (reader->lexer.record_data.rectyp == IHEX_TYPE_02_EXTENDED_SEGMENT_ADDRESS_RECORD)
		{
			reader->lexer.ext_offset = 0x00000000;
			reader->lexer.ext_offset_byte_pos = 1;
		}
		else if (reader->lexer.record_data.rectyp == IHEX_TYPE_04_EXTENDED_LINEAR_ADDRESS_RECORD)
		{
			reader->lexer.ext_offset = 0x00000000;
			reader->lexer.ext_offset_byte_pos = 3;
		}

		break;

	case IHEX_LEXER_TOKENTYPE_DATA:
		reader->lexer.calc_chksum += token_data;
		if (reader->lexer.record_data.rectyp == IHEX_TYPE_00_DATA_RECORD)
		{
			if (reader->funcData != NULL)
				message = reader->funcData((reader->lexer.ext_offset + reader->lexer.record_data.load_offset) + reader->lexer.rec_byte_offset, token_data);
		}
		else if (reader->lexer.record_data.rectyp == IHEX_TYPE_02_EXTENDED_SEGMENT_ADDRESS_RECORD)
		{
			
			reader->lexer.ext_offset |= (uint32_t)token_data << (8 * reader->lexer.ext_offset_byte_pos);
			
			if (reader->lexer.ext_offset_byte_pos == 0)
				reader->lexer.ext_offset <<= 4;
			
			reader->lexer.ext_offset_byte_pos--;
		}
		else if (reader->lexer.record_data.rectyp == IHEX_TYPE_04_EXTENDED_LINEAR_ADDRESS_RECORD)
		{
			reader->lexer.ext_offset |= (uint32_t)token_data << (8 * reader->lexer.ext_offset_byte_pos);
			reader->lexer.ext_offset_byte_pos--;
		}
		reader->lexer.rec_byte_offset++;
		break;

	case IHEX_LEXER_TOKENTYPE_CHKSUM:
		reader->lexer.calc_chksum = ~reader->lexer.calc_chksum + 1;

		if (reader->lexer.record_data.rectyp == IHEX_TYPE_01_END_OF_FILE_RECORD)
			message = IHEX_MESSAGE_END;
		else if (reader->lexer.calc_chksum != reader->lexer.record_data.target_check_sum)
			message = IHEX_MESSAGE_CHECK_SUM_ERROR;

		break;
	}

	return message;
}

static bool ihex_IsValidData(uint8_t data)
{
	return	(data == ':') ||
		((data >= '0') && (data <= '9')) ||
		((data >= 'A') && (data <= 'F')) ||
		((data >= 'a') && (data <= 'f'));
}

static int8_t ihex_CharToValue(uint8_t data)
{
	if ((data >= '0') && (data <= '9'))
		return data - '0';

	if ((data >= 'A') && (data <= 'F'))
		return data - 'A' + 10;

	if ((data >= 'a') && (data <= 'f'))
		return data - 'a' + 10;

	return -1;
}