/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * Copyright 2018 Gal Zaidenstein.
 * Copyright 2025 github.com/paul356
 */

#pragma once
#include "esp_err.h"
#include "nvs.h"

esp_err_t nvs_read_blob(const char* namespace, const char* key, void* buffer, size_t* buf_size);

esp_err_t nvs_write_blob(const char* namespace, const char* key, const void* buffer, size_t buf_size);
