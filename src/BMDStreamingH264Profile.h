#ifndef BMDStreamingH264Profile_h
#define BMDStreamingH264Profile_h

#include "const_str.h"
#include "../DeckLinkAPI_h.h"

static const void* BMDStreamingH264Profile_pairs[] =
{
    (void*)bmdStreamingH264ProfileHigh, "High",
    (void*)bmdStreamingH264ProfileMain, "Main",
    (void*)bmdStreamingH264ProfileBaseline, "Baseline",
    NULL, NULL
};

#pragma warning(push)
#pragma warning(disable: 4311)
#pragma warning(disable: 4302)
CONST_FROM_CHAR(BMDStreamingH264Profile, BMDStreamingH264Profile_pairs);
CONST_TO_CHAR(BMDStreamingH264Profile, BMDStreamingH264Profile_pairs);
#pragma warning(pop)

#endif
