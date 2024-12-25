// source/meshutils.cpp
#include "tier0/platform.h"

void GenerateSequentialIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex)
{
    for (int i = 0; i < nIndexCount; ++i)
    {
        pIndices[i] = nFirstVertex + i;
    }
}

void GenerateQuadIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex)
{
    int numQuads = nIndexCount / 6;
    int vertex = nFirstVertex;
    int index = 0;

    for (int i = 0; i < numQuads; ++i)
    {
        // First triangle
        pIndices[index++] = vertex;
        pIndices[index++] = vertex + 1;
        pIndices[index++] = vertex + 2;

        // Second triangle
        pIndices[index++] = vertex;
        pIndices[index++] = vertex + 2;
        pIndices[index++] = vertex + 3;

        vertex += 4;
    }
}

void GeneratePolygonIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex)
{
    // Triangulate polygon as a triangle fan
    int numTriangles = nIndexCount / 3;
    for (int i = 0; i < numTriangles; ++i)
    {
        pIndices[i * 3 + 0] = nFirstVertex;
        pIndices[i * 3 + 1] = nFirstVertex + i + 1;
        pIndices[i * 3 + 2] = nFirstVertex + i + 2;
    }
}

void GenerateLineStripIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex)
{
    int numLines = nIndexCount / 2;
    int index = 0;

    for (int i = 0; i < numLines; ++i)
    {
        pIndices[index++] = nFirstVertex + i;
        pIndices[index++] = nFirstVertex + i + 1;
    }
}

void GenerateLineLoopIndexBuffer(unsigned short* pIndices, int nIndexCount, int nFirstVertex)
{
    int numLines = nIndexCount / 2;
    int index = 0;

    for (int i = 0; i < numLines - 1; ++i)
    {
        pIndices[index++] = nFirstVertex + i;
        pIndices[index++] = nFirstVertex + i + 1;
    }

    // Close the loop
    pIndices[index++] = nFirstVertex + numLines - 1;
    pIndices[index] = nFirstVertex;
}