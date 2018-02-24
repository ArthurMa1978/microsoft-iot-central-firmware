// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. 

#include "Arduino.h"
#include "OledDisplay.h"

#include "../inc/oledAnimation.h"

static int frameCount;
static int move;

static unsigned char xs = 0;
static unsigned char ys = 0;
static unsigned char xe = 128;
static unsigned char ye = 8;

const unsigned char block[]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const unsigned char blockGap[]  = {0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E};
const unsigned char blockVGap[]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
const unsigned char cross[]  = {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81};
const unsigned char diagRL[] = {0xC0, 0xE0, 0x70, 0x38, 0x1C, 0x0E, 0x07, 0x03};
const unsigned char diagLR[] = {0x03, 0x07, 0x0E, 0x1C, 0x38, 0x70, 0xE0, 0xC0};
const unsigned char horzB[]  = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
const unsigned char horzT[]  = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
const unsigned char vertL[]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF};
const unsigned char vertR[]  = {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char circle[] = {0x18, 0x7E, 0x7E, 0xFF, 0xFF, 0x7E, 0x7E, 0x18};
const unsigned char clear[]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char borderBottom[]  = {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0};
const unsigned char borderTop[]  = {0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03};
const unsigned char borderLeft[]  = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char borderRight[]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF};
const unsigned char cornerLB[]  = {0xFF, 0xFF, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0};
const unsigned char cornerRB[]  = {0xC0, 0x00, 0xC0, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF};
const unsigned char cornerLT[]  = {0xFF, 0xFF, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03};
const unsigned char cornerRT[]  = {0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0xFF, 0xFF};
const unsigned char circleLT[]  = {0x00, 0xF0, 0xF8, 0xFC, 0xFC, 0xFE, 0xFE, 0xFE};
const unsigned char circleRT[]  = {0xFE, 0xFE, 0xFE, 0xFC, 0xFC, 0xF8, 0xF0, 0x00};
const unsigned char circleLB[]  = {0x00, 0x0F, 0x1F, 0x3F, 0x3F, 0x7F, 0x7F, 0x7F};
const unsigned char circleRB[]  = {0x7F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x0F, 0x00};

static unsigned char* buf;
static unsigned char* blankBuf;

static int _maxFrames;
static int _width;
static int _moveLimit;
static int _frameDelay;
static bool _center;
static const char **_frames;

void animationInit(const char **frames, int maxFrames, int width, int moveLimit, int frameDelay, bool center) {
    frameCount = 0;
    move = 0;

    _frames = frames;
    _maxFrames = maxFrames - 1;
    _width = width;
    _moveLimit = moveLimit;
    _frameDelay = frameDelay;
    _center = center;

    buf = (unsigned char*)calloc(1024, 1);
    blankBuf = (unsigned char*)calloc(64, 1);
    
    renderNextFrame();
}

void clearScreen() {
    memset(buf, 0x00, 1024);
    Screen.draw(0, 0, 128, 64, buf);
}

void renderNextFrame() {
    int columnPad = 3;
    
    memset(buf, 0x00, 1024);
    const char *image;

    image = _frames[frameCount];

    if (frameCount < _maxFrames)
        frameCount++;
    else
        frameCount = 0;
    int colLimit = _width / 8;

    for(int y = 0; y < 8; y++) {
        for(int x = 0; x < colLimit; x++) {
            if (image[(y * colLimit) + x] == 'B')
                memcpy(buf + columnPad, block, 8);
            else if (image[(y * colLimit) + x] == 'G')
                memcpy(buf + columnPad, blockGap, 8);
            else if (image[(y * colLimit) + x] == 'g')
                memcpy(buf + columnPad, blockVGap, 8);                
            else if (image[(y * colLimit) + x] == 'X')
                memcpy(buf + columnPad, cross, 8);
            else if (image[(y * colLimit) + x] == 'L')
                memcpy(buf + columnPad, diagLR, 8);
            else if (image[(y * colLimit) + x] == 'R')
                memcpy(buf + columnPad, diagRL, 8);
            else if (image[(y * colLimit) + x] == 'H')
                memcpy(buf + columnPad, horzT, 8);
            else if (image[(y * colLimit) + x] == 'h')
                memcpy(buf + columnPad, horzB, 8);
            else if (image[(y * colLimit) + x] == 'V')
                memcpy(buf + columnPad, vertL, 8);
            else if (image[(y * colLimit) + x] == 'v')
                memcpy(buf + columnPad, vertR, 8);
            else if (image[(y * colLimit) + x] == 'O')
                memcpy(buf + columnPad, circle, 8);
            else if (image[(y * colLimit) + x] == '.')
                memcpy(buf + columnPad, clear, 8);
            else if (image[(y * colLimit) + x] == 'T')
                memcpy(buf + columnPad, borderTop, 8);
            else if (image[(y * colLimit) + x] == 'b')
                memcpy(buf + columnPad, borderBottom, 8);
            else if (image[(y * colLimit) + x] == '<')
                memcpy(buf + columnPad, borderLeft, 8);
            else if (image[(y * colLimit) + x] == '>')
                memcpy(buf + columnPad, borderRight, 8);
            else if (image[(y * colLimit) + x] == '1')
                memcpy(buf + columnPad, cornerLT, 8);
            else if (image[(y * colLimit) + x] == '2')
                memcpy(buf + columnPad, cornerRT, 8);
            else if (image[(y * colLimit) + x] == '3')
                memcpy(buf + columnPad, cornerLB, 8);
            else if (image[(y * colLimit) + x] == '4')
                memcpy(buf + columnPad, cornerRB, 8);
            else if (image[(y * colLimit) + x] == '!')
                memcpy(buf + columnPad, circleLT, 8);
            else if (image[(y * colLimit) + x] == '@')
                memcpy(buf + columnPad, circleRT, 8);
            else if (image[(y * colLimit) + x] == '#')
                memcpy(buf + columnPad, circleLB, 8);
            else if (image[(y * colLimit) + x] == '$')
                memcpy(buf + columnPad, circleRB, 8);

            columnPad = columnPad + 8;
        }
        columnPad = columnPad + 8;
    }
    
    if (_moveLimit > 0)
        Screen.draw(xs + move - 8, ys, xs+move, 8, blankBuf);

    int centerPad = 0;
    if (_center)
        centerPad = (126 - _width) / 2;

    xe = _width + 8 + move + centerPad;
    Screen.draw(xs + move + centerPad, ys, xe, ye, buf);

    if (move / 8 < _moveLimit)
        move = move + 8;
    else
        move = 0;
    
    if (_frameDelay > 0)
        delay(_frameDelay);
}

void animationEnd() {
    free(buf);
    free(blankBuf);
}