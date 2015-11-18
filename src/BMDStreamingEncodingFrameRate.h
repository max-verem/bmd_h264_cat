#ifndef BMDStreamingEncodingFrameRate_h
#define BMDStreamingEncodingFrameRate_h

#include "const_str.h"
#include "../DeckLinkAPI_h.h"

#define _CRT_SECURE_NO_WARNINGS

static const void* BMDStreamingEncodingFrameRate_pairs[] =
{
    (void*)bmdStreamingEncodedFrameRate50i, "50i",
    (void*)bmdStreamingEncodedFrameRate5994i, "5994i",
    (void*)bmdStreamingEncodedFrameRate60i, "60i",
    (void*)bmdStreamingEncodedFrameRate2398p, "2398p",
    (void*)bmdStreamingEncodedFrameRate24p, "24p",
    (void*)bmdStreamingEncodedFrameRate25p, "25p",
    (void*)bmdStreamingEncodedFrameRate2997p, "2997p",
    (void*)bmdStreamingEncodedFrameRate30p, "30p",
    (void*)bmdStreamingEncodedFrameRate50p, "50p",
    (void*)bmdStreamingEncodedFrameRate5994p, "5994p",
    (void*)bmdStreamingEncodedFrameRate60p, "60p",
    NULL, NULL
};

#pragma warning(push)
#pragma warning(disable: 4311)
#pragma warning(disable: 4302)
CONST_FROM_CHAR(BMDStreamingEncodingFrameRate, BMDStreamingEncodingFrameRate_pairs);
CONST_TO_CHAR(BMDStreamingEncodingFrameRate, BMDStreamingEncodingFrameRate_pairs);
#pragma warning(pop)

#endif
