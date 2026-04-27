/**
 * utils.h - Math Utilities and Common Helpers
 * CMapNav - Terminal Navigation System
 */

#ifndef UTILS_H
#define UTILS_H

/* Great-circle distance using the Haversine formula */
double haversine_distance(double lat1, double lon1,
                          double lat2, double lon2);

/* Travel time estimate in minutes given distance (km) and speed (km/h) */
double travel_time_min(double distance_km, int speed_limit);

/* Bearing from point 1 to point 2 in degrees (0 = North) */
double bearing(double lat1, double lon1, double lat2, double lon2);

/* Direction string from bearing ("North", "Turn right", …) */
const char *compass_direction(double bearing_deg);

/* Safe string copy (always NUL-terminates) */
void safe_strcpy(char *dst, const char *src, int dst_size);

/* Strip leading/trailing whitespace in-place */
void str_trim(char *s);

/* Print a horizontal separator line */
void print_separator(char ch, int width);

#endif /* UTILS_H */
