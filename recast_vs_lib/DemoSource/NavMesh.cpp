#include "NavMesh.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "platform.h"
#include "common.h"
#include <vector>


struct NavMeshSetHeader
{
	int version;
	int tileCount;
	dtNavMeshParams params;
};

struct NavMeshTileHeader
{
	dtTileRef tileRef;
	int dataSize;
};

NavMesh::NavMesh()
{
	navmeshLayer.pNavmesh = NULL;
	navmeshLayer.pNavmeshQuery = NULL;

	printf("create new navMesh");
}


NavMesh::~NavMesh()
{
	if (navmeshLayer.pNavmesh) {
		dtFreeNavMesh(navmeshLayer.pNavmesh);
	}
	if (navmeshLayer.pNavmeshQuery) {
		dtFreeNavMeshQuery(navmeshLayer.pNavmeshQuery);
	}
	printf("release navmesh");
}


bool NavMesh::create(std::string path)
{
	FILE* fp = fopen(path.c_str(), "rb");
	if (!fp) 
	{
		printf("can't find navmesh file");
		return false;
	}

	fseek(fp, 0, SEEK_END);
	size_t flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	uint8* data = new uint8[flen];
	if (data == NULL)
	{
		printf("memory error\n");
		fclose(fp);
		SAFE_RELEASE_ARRAY(data);
		return false;
	}

	size_t readsize = fread(data, 1, flen, fp);
	if (readsize != flen)
	{ 
		printf("read data error\n");
		fclose(fp);
		SAFE_RELEASE_ARRAY(data);
		return false;
	}

	if (readsize < sizeof(NavMeshSetHeader))
	{
		printf("readsize error\n");
		fclose(fp);
		SAFE_RELEASE_ARRAY(data);
		return false;
	}

	

	bool suc= create_core(data, flen);
	fclose(fp);
	SAFE_RELEASE_ARRAY(data);
	return suc;
}

bool NavMesh::create_core(uint8* data, size_t flen) {
	bool safeStorage = true;
	int pos = 0;
	int size = sizeof(NavMeshSetHeader);

	NavMeshSetHeader header;
	memcpy(&header, data, size);

	pos += size;

	if (header.version != NavMesh::RCN_NAVMESH_VERSION)
	{
		printf("navmesh version error\n");
		 
		return false;
	}

	dtNavMesh* mesh = dtAllocNavMesh();
	if (!mesh)
	{
		printf("create navmesh error\n");
		 
		return false;
	}

	dtStatus status = mesh->init(&header.params);
	if (dtStatusFailed(status))
	{
		printf("mesh init error\n");
		 
		return false;
	}

	// Read tiles.
	bool success = true;
	for (int i = 0; i < header.tileCount; ++i)
	{
		NavMeshTileHeader tileHeader;
		size = sizeof(NavMeshTileHeader);
		memcpy(&tileHeader, &data[pos], size);
		pos += size;

		size = tileHeader.dataSize;
		if (!tileHeader.tileRef || !tileHeader.dataSize)
		{
			success = false;
			status = DT_FAILURE + DT_INVALID_PARAM;
			break;
		}

		unsigned char* tileData =
			(unsigned char*)dtAlloc(size, DT_ALLOC_PERM);
		if (!tileData)
		{
			success = false;
			status = DT_FAILURE + DT_OUT_OF_MEMORY;
			break;
		}
		memcpy(tileData, &data[pos], size);
		pos += size;

		status = mesh->addTile(tileData
			, size
			, (safeStorage ? DT_TILE_FREE_DATA : 0)
			, tileHeader.tileRef
			, 0);

		if (dtStatusFailed(status))
		{
			success = false;
			break;
		}
	}

	if (!success)
	{
		printf("NavMeshHandle::create:  %d\n", status);
		dtFreeNavMesh(mesh);
		return false;
	}

	dtNavMeshQuery* pMavmeshQuery = new dtNavMeshQuery();

	pMavmeshQuery->init(mesh, 1024);

	navmeshLayer.pNavmeshQuery = pMavmeshQuery;
	navmeshLayer.pNavmesh = mesh;

	uint32 tileCount = 0;
	uint32 nodeCount = 0;
	uint32 polyCount = 0;
	uint32 vertCount = 0;
	uint32 triCount = 0;
	uint32 triVertCount = 0;
	uint32 dataSize = 0;

	const dtNavMesh* navmesh = mesh;
	for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = navmesh->getTile(i);
		if (!tile || !tile->header)
			continue;

		tileCount++;
		nodeCount += tile->header->bvNodeCount;
		polyCount += tile->header->polyCount;
		vertCount += tile->header->vertCount;
		triCount += tile->header->detailTriCount;
		triVertCount += tile->header->detailVertCount;
		dataSize += tile->dataSize;



		printf("%f %f %f\n", tile->verts[0], tile->verts[1], tile->verts[2]);

		for (int m = 0; m < tile->header->detailVertCount; m++) {
			printf("%f %f %f\n", tile->detailVerts[m * 3 + 0], tile->detailVerts[m * 3 + 1], tile->detailVerts[m * 3 + 2]);
		}

		// DEBUG_MSG(fmt::format("NavMeshHandle::create: verts({}, {}, {})\n", tile->verts[0], tile->verts[1], tile->verts[2]));
	}

	printf("\t==> tiles loaded: %d\n", tileCount);
	printf("\t==> BVTree nodes: %d\n", nodeCount);
	printf("\t==> %d polygons (%d vertices)\n", polyCount, vertCount);
	printf("\t==> %d triangles (%d vertices)\n", triCount, triVertCount);
	printf("\t==> %.2f MB of data (not including pointers)\n", (((float)dataSize / sizeof(unsigned char)) / 1048576));
	return true;
 
}

int NavMesh::findStraightPath(const float* start, const float* end, std::vector<Position3D>& paths)
{

	// recast navmesh�Ĳ�ѯ�ӿڡ�
	dtNavMeshQuery* navmeshQuery = navmeshLayer.pNavmeshQuery;

	// �������������Ѱ·��صĲ���
	dtQueryFilter filter;
	filter.setIncludeFlags(0xffff);
	filter.setExcludeFlags(0);

	// ��Ҳ����ز���������ʲô�õ�
	const float extents[3] = { 2.f, 4.f, 2.f };

	dtPolyRef startRef = INVALID_NAVMESH_POLYREF;
	dtPolyRef endRef = INVALID_NAVMESH_POLYREF;

	// ����ʼ������ĵ�
	float startNearestPt[3];
	// ���յ�����ĵ�
	float endNearestPt[3];
	// ��������Ķ���Σ�����startRef�С�
	navmeshQuery->findNearestPoly(start, extents, &filter, &startRef, startNearestPt);
	// ��������Ķ���Σ�����endRef�С�
	navmeshQuery->findNearestPoly(end, extents, &filter, &endRef, endNearestPt);

	// ������ҵ���ʼ�ͽ��Ķ����
	if (!startRef || !endRef)
	{
		printf("NavMeshHandle::findStraightPath\n");
		return NAV_ERROR_NEARESTPOLY;
	}

	// ׼������Ѱ·�Ķ���Σ������MAX_POLYS�����񡣳��������ľͲ��Ž�ȥ�ˡ�
	dtPolyRef polys[MAX_POLYS];
	// ���ҵ�������
	int npolys;
	// ���·����
	float straightPath[MAX_POLYS * 3];
	// pathFlags��ʲô��
	unsigned char straightPathFlags[MAX_POLYS];
	dtPolyRef straightPathPolys[MAX_POLYS];
	int nstraightPath;
	int pos = 0;

	// Ѱ·
	navmeshQuery->findPath(startRef, endRef, startNearestPt, endNearestPt, &filter, polys, &npolys, MAX_POLYS);
	nstraightPath = 0;

	if (npolys)
	{
		// ����в��ҵ���
		float epos1[3];
		// ���Ƶ�
		dtVcopy(epos1, endNearestPt);

		if (polys[npolys - 1] != endRef)
			navmeshQuery->closestPointOnPoly(polys[npolys - 1], endNearestPt, epos1, 0);
		// �������Ż�·��������·��Ϊֱ·
		navmeshQuery->findStraightPath(startNearestPt, endNearestPt, polys, npolys, straightPath, straightPathFlags, straightPathPolys, &nstraightPath, MAX_POLYS);

		Position3D currpos;
		for (int i = 0; i < nstraightPath * 3; )
		{
			currpos.x = straightPath[i++];
			currpos.y = straightPath[i++];
			currpos.z = straightPath[i++];
			paths.push_back(currpos);
			pos++;

			//DEBUG_MSG(fmt::format("NavMeshHandle::findStraightPath: {}->{}, {}, {}\n", pos, currpos.x, currpos.y, currpos.z));
		}
	}
	return pos;
}
	int NavMesh::raycast(const float* start, const float* end, std::vector<Position3D>& hitPoints)
	{
		float hitPoint[3];
		// recast navmesh�Ĳ�ѯ�ӿڡ�
		dtNavMeshQuery* navmeshQuery = navmeshLayer.pNavmeshQuery;

		// �������������Ѱ·��صĲ���
		dtQueryFilter filter;
		filter.setIncludeFlags(0xffff);
		filter.setExcludeFlags(0);

		// ��Ҳ����ز���������ʲô�õ�
		const float extents[3] = { 2.f, 4.f, 2.f };

		dtPolyRef startRef = INVALID_NAVMESH_POLYREF;

		float nearestPt[3];

		navmeshQuery->findNearestPoly(start, extents, &filter, &startRef, nearestPt);

		if (!startRef) {
			return NAV_ERROR_NEARESTPOLY;
		}

		float t = 0;
		float hitNormal[3];
		memset(hitNormal,0,sizeof(hitNormal));

		dtPolyRef polys[MAX_POLYS];
		int npolys;

		navmeshQuery->raycast(startRef, start, end, &filter, &t, hitNormal, polys, &npolys, MAX_POLYS);
		if (t > 1) {
			// no hit;
			return NAV_ERROR;
		}
		else {
			// hit
			hitPoint[0] = start[0] + (end[0] - start[0])*t;
			hitPoint[1] = start[1] + (end[1] - start[1])*t;
			hitPoint[2] = start[2] + (end[2] - start[2])*t;
			if (npolys) {
				float h = 0;
				navmeshQuery->getPolyHeight(polys[npolys - 1], hitPoint, &h);
				hitPoint[1] = h;
			}
		}
		Position3D pos;
		pos.x = hitPoint[0];
		pos.y = hitPoint[1];
		pos.z = hitPoint[2];
		hitPoints.push_back(pos);
		return 1;
	}

	
 