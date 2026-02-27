#include "AIProcess.hpp"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

AIProcess::AIProcess(const std::string& path,
                     const std::string& algo, int depth) : name_(path) {
    int pipeIn[2], pipeOut[2];
    pipe2(pipeIn,  O_CLOEXEC);
    pipe2(pipeOut, O_CLOEXEC);

    std::string depthArg = std::to_string(depth);

    pid_ = fork();
    if (pid_ == 0) {
        dup2(pipeIn[0],  STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[0]);  close(pipeIn[1]);
        close(pipeOut[0]); close(pipeOut[1]);
        execl(path.c_str(), path.c_str(), algo.c_str(), depthArg.c_str(), nullptr);
        _exit(1);
    }

    // --- Processus parent ---
    close(pipeIn[0]);
    close(pipeOut[1]);
    toAI_   = pipeIn[1];
    fromAI_ = pipeOut[0];
}

AIProcess::~AIProcess() {
    if (pid_ > 0) {
        close(toAI_);
        close(fromAI_);
        waitpid(pid_, nullptr, 0);
    }
}

void AIProcess::sendBoard(const Board& b, Player p) {
    char buf[1024];
    int  n = std::snprintf(buf, sizeof(buf),
                           "%d %d %d\n", b.width(), b.height(), (int)p);
    for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width();  ++x)
        n += std::snprintf(buf + n, sizeof(buf) - n, "%d ", (int)b.get(x, y));
    buf[n++] = '\n';
    write(toAI_, buf, n);
}

Move AIProcess::readMove() {
    char buf[64];
    int  total = 0;
    char c;
    while (read(fromAI_, &c, 1) == 1) {
        buf[total++] = c;
        if (c == '\n' || total >= 63) break;
    }
    buf[total] = '\0';
    int x1, y1, x2, y2;
    std::sscanf(buf, "((%d,%d),(%d,%d))", &x1, &y1, &x2, &y2);
    return {(int8_t)x1, (int8_t)y1, (int8_t)x2, (int8_t)y2};
}

Move AIProcess::chooseMove(const Board& b, Player p) {
    sendBoard(b, p);
    return readMove();
}
