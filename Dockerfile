FROM pytorch/pytorch:2.1.0-cuda12.1-cudnn8-devel
USER root:root

ARG IMAGE_NAME=None
ARG BUILD_NUMBER=None
ARG DEBIAN_FRONTEND=noninteractive

ENV com.nvidia.cuda.version $CUDA_VERSION
ENV com.nvidia.volumes.needed nvidia_driver
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8
ENV LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/cuda/lib64:/usr/local/cuda/extras/CUPTI/lib64
ENV NCCL_DEBUG=INFO
ENV HOROVOD_GPU_ALLREDUCE=NCCL

# Install Common Dependencies
RUN apt-get update && \
    # Others
    apt-get install -y libksba8 \
    openssl \
    libaio-dev \
    git \
    wget

RUN pip install ninja -U
ENV MAX_JOBS=4

# RUN git clone https://github.com/HazyResearch/flash-fft-conv.git
COPY flash-fft-conv ./flash-fft-conv

RUN cd flash-fft-conv && \
    cd csrc/flashfftconv && \
    python setup.py install && \
    cd ../.. && \
    python setup.py install 

RUN pip install pytest einops
