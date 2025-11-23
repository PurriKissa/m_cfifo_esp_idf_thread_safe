/**
 * @file m_cfifo.c
 * @brief Implementation of circular FIFO buffer operations and cascading logic.
 *
 * This file contains the internal logic for managing circular FIFO buffers,
 * including pointer handling, usage tracking, and chained FIFO traversal.
 *
 * The implementation provides:
 * - Internal versions of push/pop/clear/state operations
 * - Static helper functions for pointer incrementing and adjacency lookup
 * - Public API wrappers with expected semaphore protection points
 *
 * Design notes:
 * - Read/write indices wrap using modulo buffer size.
 * - When no buffer is configured, pop operations return the dummy byte.
 * - Cascading enables multi-buffer storage through linked FIFO structures.
 *
 * @warning Internal functions must not be called directly outside this module.
 *
 * @see m_cfifo.h
 * @author Martin Langbein
 * @date 2025-11-23
 * @copyright GPLv2
 */


#include "m_cfifo.h"
#include <stddef.h>


//*****************************************************************************
// Local Defines
//*****************************************************************************
#define M_CFIFO_TIMEOUT 1000


//*****************************************************************************
// Local Function Prototypes
//*****************************************************************************

/**
 * @brief Internal push operation for a single FIFO instance.
 *
 * Inserts one byte into the FIFO if space is available.
 * This function does not perform semaphore protection and is intended
 * to be called only from public API wrappers.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @param data  Byte value to store.
 *
 * @retval true  Data successfully written.
 * @retval false FIFO is full or unconfigured.
 */
static bool m_cfifo_This_PushInternal(m_cfifo_tCFifo* cfifo, uint8_t data);


/**
 * @brief Internal pop operation for a single FIFO instance.
 *
 * Removes and returns the oldest byte from the FIFO.
 * If no buffer is assigned, the configured dummy byte is returned instead.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @param data  Output pointer for retrieved byte (may be NULL).
 *
 * @retval true  Data successfully read.
 * @retval false FIFO is empty.
 */
static bool m_cfifo_This_PopInternal(m_cfifo_tCFifo* cfifo, uint8_t* data);


/**
 * @brief Internal reset of FIFO read/write state.
 *
 * Clears all stored data by resetting read/write pointers
 * and usage counter to zero.
 *
 * @param cfifo Pointer to the FIFO instance.
 */
static void m_cfifo_This_ClearInternal(m_cfifo_tCFifo* cfifo);


/**
 * @brief Internal operation to mark the FIFO as full.
 *
 * Sets the usage count equal to the configured buffer size
 * without modifying buffer content.
 *
 * @param cfifo Pointer to the FIFO instance.
 */
static void m_cfifo_This_SetFullInternal(m_cfifo_tCFifo* cfifo);


/**
 * @brief Internal getter for FIFO capacity.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @return Configured buffer size in bytes.
 */
static uint16_t m_cfifo_This_GetSizeInternal(m_cfifo_tCFifo* cfifo);


/**
 * @brief Internal getter for currently stored data count.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @return Number of used bytes.
 */
static uint16_t m_cfifo_This_GetUsageInternal(m_cfifo_tCFifo* cfifo);


/**
 * @brief Internal empty-state evaluation.
 *
 * @param cfifo Pointer to the FIFO instance.
 *
 * @retval true  No data stored.
 * @retval false At least one byte available.
 */
static bool m_cfifo_This_IsEmptyInternal(m_cfifo_tCFifo* cfifo);


/**
 * @brief Internal full-state evaluation.
 *
 * @param cfifo Pointer to the FIFO instance.
 *
 * @retval true  Usage count is at or beyond capacity.
 * @retval false Space remains for at least one byte.
 */
static bool m_cfifo_This_IsFullInternal(m_cfifo_tCFifo* cfifo);


/**
 * @brief Advances the read pointer of the FIFO.
 *
 * Increments the read index and wraps using modulo `buffer_size`.
 * Intended only for use inside internal FIFO operations.
 *
 * @param cfifo Pointer to the FIFO instance.
 */
static void m_cfifo_IncRdPtr(m_cfifo_tCFifo* cfifo);


/**
 * @brief Advances the write pointer of the FIFO.
 *
 * Increments the write index and wraps using modulo `buffer_size`.
 * Intended only for use inside internal FIFO operations.
 *
 * @param cfifo Pointer to the FIFO instance.
 */
static void m_cfifo_IncWrPtr(m_cfifo_tCFifo* cfifo);


/**
 * @brief Retrieves an adjacent FIFO based on traversal direction.
 *
 * Returns either the `next` or `prev` FIFO depending on the specified
 * direction. Used during cascading operations.
 *
 * @param cfifo     Pointer to the current FIFO (may be NULL).
 * @param direction Traversal direction (UP = next, DOWN = prev).
 *
 * @return Pointer to the adjacent FIFO, or NULL if unavailable.
 */
static m_cfifo_tCFifo* m_cfifo_GetAdjacentFifo(m_cfifo_tCFifo* cfifo, m_cfifo_tDirection direction);



//*****************************************************************************
// Global Functions
//*****************************************************************************

bool m_cfifo_InitBuffer(m_cfifo_tCFifo* cfifo)
{
  cfifo->semaphore = xSemaphoreCreateBinary();
  if (cfifo->semaphore == NULL)
    return false;

  cfifo->prev = NULL;
  cfifo->next = NULL;
  cfifo->dummy_byte = 0x00;
  m_cfifo_ConfigBuffer(cfifo, NULL, 0);

  return true;
}

bool m_cfifo_CascadeAsNextBuffer(m_cfifo_tCFifo* cfifo, m_cfifo_tCFifo* cfifo_next)
{
  if (!cfifo || !cfifo_next)
    return false;
  
    if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  cfifo->next        = cfifo_next;
  cfifo_next->prev   = cfifo;
  
  xSemaphoreGive(cfifo->semaphore);

  return true;
}

bool m_cfifo_ConfigBuffer(m_cfifo_tCFifo* cfifo, const void* buffer, uint16_t buffer_size)
{
  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  cfifo->buffer      = (uint8_t*)buffer;
  cfifo->buffer_size = buffer_size;
  m_cfifo_This_SetFullInternal(cfifo);
  xSemaphoreGive(cfifo->semaphore);
}

bool m_cfifo_SetDummyByte(m_cfifo_tCFifo* cfifo, uint8_t data)
{
  if (!cfifo)
    return false;
  
    if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;
  cfifo->dummy_byte = data;
  xSemaphoreGive(cfifo->semaphore);
}  

bool m_cfifo_This_Push(m_cfifo_tCFifo* cfifo, uint8_t data)
{
  bool res;

  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

    res = m_cfifo_This_PushInternal(cfifo, data);

    xSemaphoreGive(cfifo->semaphore);
    return res;
}

bool m_cfifo_All_Push(m_cfifo_tCFifo* cfifo, uint8_t data)
{
  bool success;

  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  m_cfifo_tCFifo* actual_buffer = cfifo;
  success = false;
  
  while (!success && actual_buffer != NULL)
  {
    success = m_cfifo_This_PushInternal(actual_buffer, data);
    actual_buffer = actual_buffer->next;
  }
  
  xSemaphoreGive(cfifo->semaphore);
  return success;
}

bool m_cfifo_This_Pop(m_cfifo_tCFifo* cfifo, uint8_t* data)
{
  bool res;
  
  if (!cfifo || !data)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  res = m_cfifo_This_PopInternal(cfifo, data);

  xSemaphoreGive(cfifo->semaphore);
  return res;
}

bool m_cfifo_All_Pop(m_cfifo_tCFifo* cfifo, uint8_t* data)
{
  bool success;
  
  if (!cfifo || !data)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  m_cfifo_tCFifo* actual_buffer = cfifo;
  success = false;
  
  while (!success && actual_buffer != NULL)
  {
    success = m_cfifo_This_PopInternal(actual_buffer, data);
    actual_buffer = actual_buffer->next;
  }

  xSemaphoreGive(cfifo->semaphore);
  return success;
}

bool m_cfifo_This_Clear(m_cfifo_tCFifo* cfifo)
{
  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;
  
  m_cfifo_This_ClearInternal(cfifo);

  xSemaphoreGive(cfifo->semaphore);
  return true;
}

bool m_cfifo_All_Clear(m_cfifo_tCFifo* cfifo, m_cfifo_tDirection direction)
{
    m_cfifo_tCFifo* actual_buffer = cfifo;
    
  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  while (actual_buffer != NULL)
  {
    m_cfifo_This_ClearInternal(actual_buffer);
    actual_buffer = m_cfifo_GetAdjacentFifo(actual_buffer, direction);
  }

  xSemaphoreGive(cfifo->semaphore);
  return true;
}

bool m_cfifo_This_SetFull(m_cfifo_tCFifo* cfifo)
{
  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  m_cfifo_This_SetFullInternal(cfifo);
  
  xSemaphoreGive(cfifo->semaphore);
  return true;
}

bool m_cfifo_All_SetFull(m_cfifo_tCFifo* cfifo, m_cfifo_tDirection direction)
{
    m_cfifo_tCFifo* actual_buffer = cfifo;

  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  while (actual_buffer != NULL)
  {
    m_cfifo_This_SetFullInternal(actual_buffer);
    actual_buffer = m_cfifo_GetAdjacentFifo(actual_buffer, direction);
  }

  xSemaphoreGive(cfifo->semaphore);
  return true;
}

uint16_t m_cfifo_This_GetSize(m_cfifo_tCFifo* cfifo)
{
  uint16_t res = 0;
  
  if (!cfifo)
    return res;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return res;

  res = m_cfifo_This_GetSizeInternal(cfifo);
  
  xSemaphoreGive(cfifo->semaphore);
  return res;
}

uint32_t m_cfifo_All_GetSize(m_cfifo_tCFifo* cfifo)
{
  uint32_t size_total = 0;
  
  if (!cfifo)
    return size_total;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return size_total;

  size_total = 0;
  m_cfifo_tCFifo* actual_buffer = cfifo;
 
  while (actual_buffer != NULL)
  {
    size_total += m_cfifo_This_GetSizeInternal(actual_buffer);
    actual_buffer = actual_buffer->next;
  }
  
  xSemaphoreGive(cfifo->semaphore);
  return size_total;
}

uint16_t m_cfifo_This_GetUsage(m_cfifo_tCFifo* cfifo)
{
  uint16_t res = 0;

  if (!cfifo)
    return res;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return res;

  res = m_cfifo_This_GetUsageInternal(cfifo);

  xSemaphoreGive(cfifo->semaphore);
  return res;
}

uint32_t m_cfifo_All_GetUsage(m_cfifo_tCFifo* cfifo)
{
  uint32_t total_used = 0;
  
  if (!cfifo)
    return total_used;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return total_used;

  m_cfifo_tCFifo* actual_buffer = cfifo;
  total_used = 0;
  
  while(actual_buffer != NULL)
  {
    total_used += m_cfifo_This_GetUsageInternal(actual_buffer);
    actual_buffer = actual_buffer->next;
  }

  xSemaphoreGive(cfifo->semaphore);
  return total_used;
}

bool m_cfifo_This_IsEmpty(m_cfifo_tCFifo* cfifo)
{
    bool res;

  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  res = m_cfifo_This_IsEmptyInternal(cfifo);

  xSemaphoreGive(cfifo->semaphore);
  return res;
}

bool m_cfifo_All_IsEmpty(m_cfifo_tCFifo* cfifo)
{
  bool is_empty = true;

  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  m_cfifo_tCFifo* actual_buffer = cfifo;
  
  while (actual_buffer != NULL)
  {
    is_empty = is_empty && m_cfifo_This_IsEmptyInternal(actual_buffer);
    actual_buffer = actual_buffer->next;
  }

  xSemaphoreGive(cfifo->semaphore);
  return is_empty;
}

bool m_cfifo_This_IsFull(m_cfifo_tCFifo* cfifo)
{
    bool res;
  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;

  res = m_cfifo_This_IsFullInternal(cfifo);
    
  xSemaphoreGive(cfifo->semaphore);
  return res;
}

bool m_cfifo_All_IsFull(m_cfifo_tCFifo* cfifo)
{
  bool is_full = true;

  m_cfifo_tCFifo* actual_buffer = cfifo;

  if (!cfifo)
    return false;
  
  if (xSemaphoreTake(cfifo->semaphore, M_CFIFO_TIMEOUT / portTICK_PERIOD_MS) == pdFALSE)
    return false;
  
  while (actual_buffer != NULL)
  {
    is_full = is_full && m_cfifo_This_IsFullInternal(actual_buffer);
    actual_buffer = actual_buffer->next;
  }

  xSemaphoreGive(cfifo->semaphore);
  return is_full;
}



//*****************************************************************************
// Local Functions
//*****************************************************************************

static bool m_cfifo_This_PushInternal(m_cfifo_tCFifo* cfifo, uint8_t data)
{
    if (cfifo->buffer == NULL)
        return false;

    if (m_cfifo_This_IsFullInternal(cfifo))
        return false;

    cfifo->buffer[cfifo->wrPtr] = data;
    m_cfifo_IncWrPtr(cfifo);
    cfifo->used_count++;

    return true;
}

static bool m_cfifo_This_PopInternal(m_cfifo_tCFifo* cfifo, uint8_t* data)
{
    if (m_cfifo_This_IsEmptyInternal(cfifo))
        return false;

    if (cfifo->buffer == NULL)
    {
        if (data != NULL)
            *data = cfifo->dummy_byte;
    }
    else
    {
        if (data != NULL)
            *data = cfifo->buffer[cfifo->rdPtr];
    }

    m_cfifo_IncRdPtr(cfifo);
    cfifo->used_count--;

    return true;
}

static void m_cfifo_This_ClearInternal(m_cfifo_tCFifo* cfifo)
{
    cfifo->rdPtr = 0;
    cfifo->wrPtr = 0;
    cfifo->used_count = 0;
}

static void m_cfifo_This_SetFullInternal(m_cfifo_tCFifo* cfifo)
{
    cfifo->rdPtr = 0;
    cfifo->wrPtr = 0;
    cfifo->used_count = cfifo->buffer_size;
}

static uint16_t m_cfifo_This_GetSizeInternal(m_cfifo_tCFifo* cfifo)
{
    return cfifo->buffer_size;
}

static uint16_t m_cfifo_This_GetUsageInternal(m_cfifo_tCFifo* cfifo)
{
    return cfifo->used_count;
}

static bool m_cfifo_This_IsEmptyInternal(m_cfifo_tCFifo* cfifo)
{
    bool is_empty;

    is_empty = cfifo->used_count == 0;

    return is_empty;
}

static bool m_cfifo_This_IsFullInternal(m_cfifo_tCFifo* cfifo)
{
    bool is_full;

    is_full = cfifo->used_count >= cfifo->buffer_size;

    return is_full;
}

static void m_cfifo_IncRdPtr(m_cfifo_tCFifo* cfifo)
{
  cfifo->rdPtr = (cfifo->rdPtr + 1) % cfifo->buffer_size;
}

static void m_cfifo_IncWrPtr(m_cfifo_tCFifo* cfifo)
{
  cfifo->wrPtr = (cfifo->wrPtr + 1) % cfifo->buffer_size;
}

static m_cfifo_tCFifo* m_cfifo_GetAdjacentFifo(m_cfifo_tCFifo* cfifo, m_cfifo_tDirection direction)
{
  if (cfifo == NULL)
    return NULL;
    
  if (direction == M_CFIFO_UP)
    return cfifo->next;
  else if (direction == M_CFIFO_DOWN)
    return cfifo->prev;
  else
    return NULL;
}
