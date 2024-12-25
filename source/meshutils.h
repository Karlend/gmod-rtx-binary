#pragma once

void GenerateSequentialIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex);
void GenerateQuadIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex);
void GeneratePolygonIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex);
void GenerateLineStripIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex);
void GenerateLineLoopIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex);