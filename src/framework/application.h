#pragma once

#ifndef APPLICATION_H
#define APPLICATION_H

int application_init(const char* title);
void application_cleanup();
void application_draw();
void application_update();

#endif APPLICATION_H