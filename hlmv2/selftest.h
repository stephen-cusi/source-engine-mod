//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Headless --selftest pipeline. Runs without any visible window:
//          it loads a model, walks every inspection path and finally boots
//          the GL viewport (offscreen when no display is available).
//          Output format: "SELFTEST: <step> PASS/FAIL <detail>".
//
//=============================================================================

#ifndef HLMV2_SELFTEST_H
#define HLMV2_SELFTEST_H

#include <QString>

// Runs the self-test; returns 0 when every step passed, 1 otherwise.
int RunHlmvSelfTest( const QString &mdlPath );

#endif // HLMV2_SELFTEST_H
