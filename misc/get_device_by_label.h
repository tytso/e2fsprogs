/*
 * get_device_by_label.h
 *
 * Copyright 1999 by Andries Brouwer
 * Copyright 1999, 2000 by Theodore Ts'o
 *
 * This file may be redistributed under the terms of the GNU Public
 * License. 
 */

extern char *string_copy(const char *s);
extern char *get_spec_by_uuid(const char *uuid);
extern char *get_spec_by_volume_label(const char *volumelabel);
extern const char *get_volume_label_by_spec(const char *spec);
extern char *interpret_spec(char *spec);
