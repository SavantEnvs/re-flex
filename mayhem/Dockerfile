FROM --platform=linux/amd64 ubuntu:22.04 as builder

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential clang

COPY . /repo
WORKDIR /repo
RUN ./build.sh
RUN clang++ /repo/mayhem/fuzz_range.cpp -fsanitize=fuzzer,address /repo/lib/libreflex.a -o /fuzz

FROM ubuntu:22.04 as package
COPY --from=builder /repo/bin/reflex /
COPY --from=builder /fuzz /fuzz_range
