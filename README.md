# m_cfifo - Circular FIFO Buffer for ESP-IDF

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

**Author:** Martin Langbein  
**Date:** 2025-11-23  
**License:** GPLv2  

---

## Overview

`m_cfifo` is a **thread-safe circular FIFO buffer library** designed for ESP-IDF projects.  
It supports both **single-buffer operations** and **cascading multi-buffer chains**, enabling flexible storage and retrieval of byte data.

Thread safety is ensured using **FreeRTOS semaphores**, making it suitable for multi-tasking applications.

---

## Features

- Circular FIFO buffer management (wrap-around read/write pointers)
- Push and pop operations for single FIFO and cascaded FIFO chains
- Clear, set full, and dummy byte support
- Query functions for buffer size, usage, empty/full state
- Cascading for multi-buffer storage
- Thread-safe operations using FreeRTOS semaphores
- Lightweight and minimal dependencies (requires FreeRTOS only)

---

## Installation

1. Clone or download the repository:

```bash
git clone https://github.com/yourusername/m_cfifo.git
```

Copy m_cfifo.c and m_cfifo.h into your ESP-IDF project components folder.
Add the component to your CMakeLists.txt:

idf_component_register(SRCS "m_cfifo.c"
                       INCLUDE_DIRS ".")

Initialization
```c
m_cfifo_tCFifo fifo;
if (!m_cfifo_InitBuffer(&fifo)) {
    // Handle initialization failure
}

uint8_t buffer[128];
m_cfifo_ConfigBuffer(&fifo, buffer, sizeof(buffer));
m_cfifo_SetDummyByte(&fifo, 0xFF);
```
Push / Pop
```c
// Push a single byte
m_cfifo_This_Push(&fifo, 0x42);

// Pop a single byte
uint8_t data;
if (m_cfifo_This_Pop(&fifo, &data)) {
    // data contains the popped byte
}

// Push into cascaded buffers
m_cfifo_All_Push(&fifo, 0x55);

// Pop from cascaded buffers
m_cfifo_All_Pop(&fifo, &data);
```
Query
```c
uint16_t used = m_cfifo_This_GetUsage(&fifo);
uint16_t size = m_cfifo_This_GetSize(&fifo);
bool empty  = m_cfifo_This_IsEmpty(&fifo);
bool full   = m_cfifo_This_IsFull(&fifo);
```
