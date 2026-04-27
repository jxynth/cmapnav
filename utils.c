/**
 * utils.c - Math Utilities Implementation
 * CMapNav - Terminal Navigation System
 */

#define _USE_MATH_DEFINES
#include "utils.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define EARTH_RADIUS_KM 6371.0
#define DEG_TO_RAD(x)  ((x) * M_PI / 180.0)

double haversine_distance(double lat1, double lon1,
                          double lat2, double lon2) {
    double dlat = DEG_TO_RAD(lat2 - lat1);
    double dlon = DEG_TO_RAD(lon2 - lon1);
    double rlat1 = DEG_TO_RAD(lat1);
    double rlat2 = DEG_TO_RAD(lat2);

    double a = sin(dlat/2)*sin(dlat/2)
             + cos(rlat1)*cos(rlat2)*sin(dlon/2)*sin(dlon/2);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return EARTH_RADIUS_KM * c;
}

double travel_time_min(double distance_km, int speed_limit) {
    if (speed_limit <= 0) speed_limit = 50; /* safety default */
    return (distance_km / (double)speed_limit) * 60.0;
}

double bearing(double lat1, double lon1, double lat2, double lon2) {
    double rlat1 = DEG_TO_RAD(lat1);
    double rlat2 = DEG_TO_RAD(lat2);
    double dlon  = DEG_TO_RAD(lon2 - lon1);
    double y = sin(dlon) * cos(rlat2);
    double x = cos(rlat1)*sin(rlat2) - sin(rlat1)*cos(rlat2)*cos(dlon);
    double b = atan2(y, x) * 180.0 / M_PI;
    return fmod(b + 360.0, 360.0);
}

const char *compass_direction(double b) {
    /* 8-point compass */
    if (b < 22.5  || b >= 337.5) return "North";
    if (b < 67.5)                 return "Northeast";
    if (b < 112.5)                return "East";
    if (b < 157.5)                return "Southeast";
    if (b < 202.5)                return "South";
    if (b < 247.5)                return "Southwest";
    if (b < 292.5)                return "West";
    return "Northwest";
}

void safe_strcpy(char *dst, const char *src, int dst_size) {
    if (!dst || !src || dst_size <= 0) return;
    strncpy(dst, src, (size_t)(dst_size - 1));
    dst[dst_size - 1] = '\0';
}

void str_trim(char *s) {
    if (!s) return;
    /* trim trailing */
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) s[--len] = '\0';
    /* trim leading */
    int start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, (size_t)(len - start + 1));
}

void print_separator(char ch, int width) {
    for (int i = 0; i < width; i++) putchar(ch);
    putchar('\n');
}
