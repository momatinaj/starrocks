# Elasticsearch

## Overview

Elasticsearch combines dense_vector fields with geo_shape/geo_point fields and uses ACORN-1 (adopted in Lucene 10.2.0, February 2025) for filtered HNSW search. This is the most mature production implementation of filtered vector search with geo support.

## Geo Support

**Field types**:
- `geo_point`: Latitude/longitude coordinates
- `geo_shape`: Arbitrary GeoJSON geometries (polygons, multipolygons, etc.)

**Geo queries**:
- `geo_bounding_box`: Rectangular bounds
- `geo_distance`: Points within radius
- `geo_shape`: Arbitrary shape intersection/containment (backed by BKD tree)

## Vector Support

**Field type**: `dense_vector` — fixed-dimension float vectors  
**Index**: HNSW algorithm with configurable `m`, `ef_construction`  
**Search**: kNN search API with approximate (HNSW) and exact modes  
**Distance metrics**: L2, cosine, dot product

## Combined Geo+Vector Architecture

### ACORN-1 in Lucene (Feb 2025)

Lucene 10.2.0 merged the ACORN-1 variant of filtered HNSW search. Key aspects:

**Selectivity-based strategy selection**:
- Low selectivity (≤8%): Blind 2-hop exploration — when a neighbor fails the filter, explore ALL of its neighbors (maxConn × maxConn candidates per hop)
- Medium selectivity (8-40%): Directed 2-hop — when a neighbor fails, explore only its neighbors that are geometrically closer to the query vector
- High selectivity (>40%): Standard 1-hop HNSW — most nodes pass the filter, overhead of 2-hop not worthwhile

**Query flow**:
1. Parse query with both `knn` clause and `filter` clause
2. Estimate filter selectivity
3. Select ACORN-1 strategy based on selectivity
4. Execute filtered HNSW traversal
5. Return top-k results matching both vector similarity and filter

### Geo-Semantic Search Pattern

```json
{
  "knn": {
    "field": "embedding",
    "query_vector": [0.1, 0.2, ...],
    "k": 10,
    "num_candidates": 100,
    "filter": {
      "geo_shape": {
        "location": {
          "shape": { "type": "polygon", "coordinates": [...] },
          "relation": "within"
        }
      }
    }
  }
}
```

## Strengths

- ACORN-1 is the state-of-the-art for filtered HNSW, proven at massive scale
- Rich geo support via Lucene's spatial module (BKD trees for geo_shape)
- Selectivity-adaptive strategy avoids worst-case scenarios
- Production-hardened in Elasticsearch deployments worldwide
- ES|QL support for dense_vector (first-class SQL-like query language)

## Limitations

- Geo filter is applied as a generic predicate — no spatial-index-level integration with HNSW
- BKD tree (for geo_shape) and HNSW graph are separate structures
- Filter selectivity estimation may be inaccurate for complex spatial predicates
- No spatial partitioning of the HNSW graph (all nodes in a single graph per segment)

## Relevance to StarRocks

- **ACORN-1 strategy selection** (blind 2-hop, directed 2-hop, standard 1-hop based on selectivity) is directly implementable in TenANN's HNSW traversal
- **Selectivity thresholds** (8%, 40%) provide a starting point for our cost model, though spatial predicates may need different thresholds
- **Limitation**: Even Elasticsearch doesn't exploit spatial structure within the HNSW graph — this is our opportunity
- **Lesson**: Treat ACORN-1 as the baseline to beat, not the final solution
