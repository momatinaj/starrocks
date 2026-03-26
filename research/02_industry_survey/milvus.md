# Milvus

## Overview

Milvus is a large-scale vector database (billions of vectors) with strong distributed architecture. It supports filtered search via scalar field expressions and partition keys, but has no native geo data type.

## Geo Support

**No native geo type.** Spatial data must be stored as scalar fields (separate lat/lon floats) and filtered using range expressions.

**Workaround**:
```python
# Bounding box approximation
expr = "lat >= 37.0 AND lat <= 38.0 AND lon >= -123.0 AND lon <= -122.0"
results = collection.search(
    data=[query_vector],
    anns_field="embedding",
    param={"metric_type": "L2", "params": {"nprobe": 10}},
    limit=10,
    expr=expr
)
```

This is limited to rectangular bounding boxes — no point-in-polygon, no radius queries, no complex spatial predicates.

## Vector Support

**Index types**: IVF_FLAT, IVF_SQ8, IVF_PQ, HNSW, DiskANN, SCANN, GPU indexes  
**Metrics**: L2, IP, COSINE, JACCARD, HAMMING

## Partition Key Architecture

Milvus' partition key feature is the most relevant aspect for our research:

- Designate a scalar field as partition key
- Data is automatically distributed across partitions by key value
- Search can be restricted to specific partitions via key-based filtering
- Up to 1024 partitions per collection

**For spatial use**: If location data were encoded as a spatial key (e.g., geohash prefix or H3 cell), partition-based filtering could provide coarse spatial pruning. However, this is not natively supported.

## Hybrid Search (v2.5+)

- Multi-vector field search with different metric types
- Scalar filter expressions combined with ANN
- Reranking: RRF (Reciprocal Rank Fusion) and weighted strategies
- Filter expressions support boolean logic on scalar fields

## Strengths

- Massively scalable (billions of vectors)
- Rich ANN index ecosystem (HNSW, IVF, DiskANN, GPU)
- Partition key enables coarse-grained data partitioning
- Strong distributed architecture with sharding

## Limitations

- No native geo type or spatial functions
- Scalar filter expressions limited to simple comparisons (no geometry operations)
- Partition key approach is coarse — no fine-grained spatial indexing
- No spatial-aware HNSW construction

## Relevance to StarRocks

- **Partition key concept** maps to StarRocks' table partitioning — partitioning by S2 cell level could provide coarse spatial pruning at the tablet level
- **Multi-index ecosystem** shows the value of supporting multiple ANN algorithms
- **Limitation**: Milvus' lack of native geo shows that even leading vector DBs haven't solved spatial+vector integration
