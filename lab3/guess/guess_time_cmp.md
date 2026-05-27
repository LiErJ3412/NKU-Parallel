PCFG_GENERATE_MODE=serial PCFG_THREADS=1 sh test.sh 2 1
Guess time:0.438053seconds
Hash time:1.72727seconds
Train time:30.4701seconds

PCFG_GENERATE_MODE=pthread PCFG_THREADS=2 sh test.sh 2 1
Guess time:0.442532seconds
Hash time:1.75827seconds
Train time:27.6939seconds

PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 sh test.sh 2 1
Guess time:0.450228seconds
Hash time:1.83289seconds
Train time:29.6695seconds

PCFG_GENERATE_MODE=pthread PCFG_THREADS=2 PCFG_GENERATE_THRESHOLD=100000 sh test.sh 2 1

Guess time:0.402398seconds
Hash time:1.74784seconds
Train time:29.2855seconds

PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 PCFG_GENERATE_THRESHOLD=100000 sh test.sh 2 1
Guess time:0.404749seconds
Hash time:1.61068seconds
Train time:30.2463seconds

PCFG_GENERATE_MODE=pthread PCFG_THREADS=8 PCFG_GENERATE_THRESHOLD=100000 sh test.sh 2 1
Guess time:0.423047seconds
Hash time:1.65154seconds
Train time:29.9238seconds

PCFG_GENERATE_MODE=pthread PCFG_THREADS=2 PCFG_GENERATE_THRESHOLD=200000 sh test.sh 2 1
Guess time:0.378543seconds
Hash time:1.73668seconds
Train time:29.9107seconds

PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 PCFG_GENERATE_THRESHOLD=200000 sh test.sh 2 1
Guess time:0.419853seconds
Hash time:1.73252seconds
Train time:30.4268seconds

PCFG_GENERATE_MODE=pthread PCFG_THREADS=8 PCFG_GENERATE_THRESHOLD=200000 sh test.sh 2 1
Guess time:0.415663seconds
Hash time:1.76695seconds
Train time:30.7404seconds


PCFG_GENERATE_MODE=openmp PCFG_THREADS=2 sh test.sh 2 1
Guess time:0.433636seconds
Hash time:1.73372seconds
Train time:29.5668seconds

PCFG_GENERATE_MODE=openmp PCFG_THREADS=4 sh test.sh 2 1
Guess time:0.416489seconds
Hash time:1.64985seconds
Train time:29.8786seconds

PCFG_GENERATE_MODE=openmp PCFG_THREADS=8 sh test.sh 2 1
Guess time:0.399166seconds
Hash time:1.69181seconds
Train time:29.6717seconds