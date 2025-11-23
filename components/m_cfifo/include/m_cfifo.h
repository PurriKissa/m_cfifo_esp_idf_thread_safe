/**
 * @file m_cfifo.c
 * @brief Circular FIFO buffer API for ESP-IDF projects.
 *
 * This module provides thread-safe operations on circular FIFO buffers,
 * including single-buffer operations and cascading multi-buffer management.
 *
 * Thread safety is provided via FreeRTOS semaphores.
 *
 * @author Martin Langbein
 * @date 2025-11-23
 * @copyright GPLv2
 */

#ifndef M_CFIFO_H_
#define M_CFIFO_H_


#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
//*****************************************************************************
// Global Defines
//*****************************************************************************


//*****************************************************************************
// Global Types
//*****************************************************************************

/**
 * @brief Direction selector for traversing cascaded FIFO buffers.
 *
 * This enumeration is used by multi-buffer operations such as
 * @ref m_cfifo_All_Clear, @ref m_cfifo_All_SetFull,
 * @ref m_cfifo_All_Pop, and @ref m_cfifo_All_IsEmpty
 * to determine which adjacent FIFO to access next.
 *
 * - `M_CFIFO_UP`   → move to `next` FIFO in the chain
 * - `M_CFIFO_DOWN` → move to `prev` FIFO in the chain
 */
typedef enum
{
  M_CFIFO_UP,
  M_CFIFO_DOWN
}m_cfifo_tDirection;


/**
 * @brief Control structure for a circular FIFO byte buffer.
 *
 * This structure stores all runtime state needed for FIFO operation,
 * including buffer pointers, usage tracking, and optional links to
 * adjacent FIFOs for cascading support.
 *
 * Usage requirements:
 * - Must be initialized using @ref m_cfifo_InitBuffer before use.
 * - A working data buffer may be assigned with @ref m_cfifo_ConfigBuffer.
 * - If no buffer is configured, pop operations return `dummy_byte`.
 *
 * The FIFO implements circular wrapping for both read and write indices.
 */
typedef struct _cfifo
{
  struct _cfifo* prev;
  struct _cfifo* next;

  uint8_t* buffer;
  uint16_t buffer_size;
  uint16_t used_count;
  uint16_t rdPtr;
  uint16_t wrPtr;
  
  uint8_t dummy_byte;
  SemaphoreHandle_t semaphore;
}m_cfifo_tCFifo;


//*****************************************************************************
// Global Variable Declarations (Extern)
//*****************************************************************************


//*****************************************************************************
// Global Function Prototypes
//*****************************************************************************

/**
 * @brief Initialize a FIFO structure and its internal semaphore.
 *
 * Allocates a binary semaphore for the FIFO and initializes
 * all internal pointers and settings to default values.
 *
 * @param cfifo Pointer to a FIFO instance to initialize.
 * @return true if initialization succeeded, false otherwise.
 */
bool m_cfifo_InitBuffer(m_cfifo_tCFifo* cfifo);


/**
 * @brief Link a FIFO as the next buffer in a cascade.
 *
 * Sets up a bi-directional connection between the current FIFO
 * and the next FIFO for cascading operations.
 *
 * @param cfifo Pointer to the current FIFO instance.
 * @param cfifo_next Pointer to the next FIFO to attach.
 * @return true if linking succeeded, false otherwise.
 */
bool m_cfifo_CascadeAsNextBuffer(m_cfifo_tCFifo* cfifo, m_cfifo_tCFifo* cfifo_next);


/**
 * @brief Configure the storage buffer for a FIFO instance.
 *
 * Assigns a memory region for the FIFO and sets the buffer size.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @param buffer Pointer to the memory buffer (uint8_t array).
 * @param buffer_size Size of the buffer in bytes.
 * @return true if configuration succeeded, false otherwise.
 */
bool m_cfifo_ConfigBuffer(m_cfifo_tCFifo* cfifo, const void* buffer, uint16_t buffer_size);


/**
 * @brief Set the dummy byte returned when the FIFO is empty.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @param data Dummy byte value to use.
 * @return true if the dummy byte was set successfully, false otherwise.
 */
bool m_cfifo_SetDummyByte(m_cfifo_tCFifo* cfifo, uint8_t data);


/**
 * @brief Push a byte into a single FIFO.
 *
 * Adds one byte to the FIFO if space is available. Thread-safe.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @param data Byte to push.
 * @return true if the byte was added, false if FIFO is full.
 */

bool m_cfifo_This_Push(m_cfifo_tCFifo* cfifo, uint8_t data);


/**
 * @brief Push a byte into a cascading chain of FIFOs.
 *
 * Attempts to store the byte in the first FIFO and cascades
 * to the next buffer if the current is full.
 *
 * @param cfifo Pointer to the first FIFO in the cascade.
 * @param data Byte to push.
 * @return true if the byte was successfully stored in any buffer, false otherwise.
 */
bool m_cfifo_All_Push(m_cfifo_tCFifo* cfifo, uint8_t data);



/**
 * @brief Pop a byte from a single FIFO.
 *
 * Retrieves the oldest byte from the FIFO. Returns dummy byte if FIFO is unconfigured.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @param data Pointer to store the retrieved byte.
 * @return true if a byte was retrieved, false if FIFO is empty.
 */
bool m_cfifo_This_Pop(m_cfifo_tCFifo* cfifo, uint8_t* data);


/**
 * @brief Pop a byte from a cascading chain of FIFOs.
 *
 * Attempts to retrieve a byte from the first FIFO and cascades
 * to the next if the current FIFO is empty.
 *
 * @param cfifo Pointer to the first FIFO in the cascade.
 * @param data Pointer to store the retrieved byte.
 * @return true if a byte was successfully retrieved, false otherwise.
 */
bool m_cfifo_All_Pop(m_cfifo_tCFifo* cfifo, uint8_t* data);


/**
 * @brief Clear all data from a single FIFO.
 *
 * Resets read/write pointers and usage counters.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @return true if cleared successfully, false otherwise.
 */
bool m_cfifo_This_Clear(m_cfifo_tCFifo* cfifo);


/**
 * @brief Clear all data from a cascade of FIFOs.
 *
 * Traverses the cascade in the specified direction and clears all buffers.
 *
 * @param cfifo Pointer to the starting FIFO.
 * @param direction Direction to traverse the cascade (M_CFIFO_UP or M_CFIFO_DOWN).
 * @return true if all buffers cleared successfully, false otherwise.
 */
bool m_cfifo_All_Clear(m_cfifo_tCFifo* cfifo, m_cfifo_tDirection direction);


/**
 * @brief Mark a single FIFO as full.
 *
 * Sets usage counter to buffer size without modifying content.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @return true if operation succeeded, false otherwise.
 */
bool m_cfifo_This_SetFull(m_cfifo_tCFifo* cfifo);


/**
 * @brief Mark all FIFOs in a cascade as full.
 *
 * Traverses the cascade in the given direction and marks each FIFO as full.
 *
 * @param cfifo Pointer to the starting FIFO.
 * @param direction Direction to traverse the cascade (M_CFIFO_UP or M_CFIFO_DOWN).
 * @return true if operation succeeded for all buffers, false otherwise.
 */
bool m_cfifo_All_SetFull(m_cfifo_tCFifo* cfifo, m_cfifo_tDirection direction);


/**
 * @brief Get the configured buffer size of a single FIFO.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @return Buffer size in bytes, or 0 if FIFO is unconfigured.
 */
uint16_t m_cfifo_This_GetSize(m_cfifo_tCFifo* cfifo);


/**
 * @brief Get the total configured size of a cascade of FIFOs.
 *
 * @param cfifo Pointer to the first FIFO in the cascade.
 * @return Total buffer size in bytes across all buffers.
 */
uint32_t m_cfifo_All_GetSize(m_cfifo_tCFifo* cfifo);


/**
 * @brief Get the number of bytes currently stored in a FIFO.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @return Number of used bytes.
 */
uint16_t m_cfifo_This_GetUsage(m_cfifo_tCFifo* cfifo);


/**
 * @brief Get the total number of bytes stored in a cascade of FIFOs.
 *
 * @param cfifo Pointer to the first FIFO in the cascade.
 * @return Total number of used bytes across all buffers.
 */
uint32_t m_cfifo_All_GetUsage(m_cfifo_tCFifo* cfifo);


/**
 * @brief Check if a single FIFO is empty.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @return true if empty, false otherwise.
 */
bool m_cfifo_This_IsEmpty(m_cfifo_tCFifo* cfifo);


/**
 * @brief Check if all FIFOs in a cascade are empty.
 *
 * @param cfifo Pointer to the first FIFO in the cascade.
 * @return true if all buffers are empty, false otherwise.
 */
bool m_cfifo_All_IsEmpty(m_cfifo_tCFifo* cfifo);


/**
 * @brief Check if a single FIFO is full.
 *
 * @param cfifo Pointer to the FIFO instance.
 * @return true if full, false otherwise.
 */
bool m_cfifo_This_IsFull(m_cfifo_tCFifo* cfifo);


/**
 * @brief Check if all FIFOs in a cascade are full.
 *
 * @param cfifo Pointer to the first FIFO in the cascade.
 * @return true if all buffers are full, false otherwise.
 */
bool m_cfifo_All_IsFull(m_cfifo_tCFifo* cfifo);


#endif /* M_CFIFO_H_ */