#ifndef BMDStreamingH264Level_h
#define BMDStreamingH264Level_h

#include "const_str.h"
#include "../DeckLinkAPI_h.h"

static const void* BMDStreamingH264Level_pairs[] =
{
    (void*)bmdStreamingH264Level12, "12",
    (void*)bmdStreamingH264Level13, "13",
    (void*)bmdStreamingH264Level2, "2",
    (void*)bmdStreamingH264Level21, "21",
    (void*)bmdStreamingH264Level22, "22",
    (void*)bmdStreamingH264Level3, "3",
    (void*)bmdStreamingH264Level31, "31",
    (void*)bmdStreamingH264Level32, "32",
    (void*)bmdStreamingH264Level4, "4",
    (void*)bmdStreamingH264Level41, "41",
    (void*)bmdStreamingH264Level42, "42",
    NULL, NULL
};

#pragma warning(push)
#pragma warning(disable: 4311)
#pragma warning(disable: 4302)
CONST_FROM_CHAR(BMDStreamingH264Level, BMDStreamingH264Level_pairs);
CONST_TO_CHAR(BMDStreamingH264Level, BMDStreamingH264Level_pairs);
#pragma warning(pop)

#endif /* BMDStreamingH264Level_h */
