#ifndef BMDStreamingH264EntropyCoding_h
#define BMDStreamingH264EntropyCoding_h

#include "const_str.h"
#include "../DeckLinkAPI_h.h"

static const void* BMDStreamingH264EntropyCoding_pairs[] =
{
    (void*)bmdStreamingH264EntropyCodingCAVLC, "CAVLC",
    (void*)bmdStreamingH264EntropyCodingCABAC, "CABAC",
    NULL, NULL
};

#pragma warning(push)
#pragma warning(disable: 4311)
#pragma warning(disable: 4302)
CONST_FROM_CHAR(BMDStreamingH264EntropyCoding, BMDStreamingH264EntropyCoding_pairs);
CONST_TO_CHAR(BMDStreamingH264EntropyCoding, BMDStreamingH264EntropyCoding_pairs);
#pragma warning(pop)

#endif
