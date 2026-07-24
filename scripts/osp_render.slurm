#!/bin/bash
#SBATCH -J osp_render
#SBATCH -o slurm-%x-%j.out
#SBATCH -e slurm-%x-%j.err
#SBATCH -N 1
#SBATCH -n 1
#SBATCH -t 02:00:00
#SBATCH --mail-type=ALL
#SBATCH --mail-user=jixianli@tacc.utexas.edu

# Cluster-specific: set account / partition / QoS before submitting.
#SBATCH -A <project code> # project code
#SBATCH -p development  # partition name

cd $SCRATCH/tools/osp_antar_ice/
SIF=osp_renderer.sif

# Do not use `nproc` here. TACC presets OMP_NUM_THREADS=1 in the job environment
# and coreutils nproc returns that instead of the affinity-mask size, so it
# reports 1 on a node where all 128 cores are ours.
RENDER_THREADS=${SLURM_CPUS_ON_NODE:-$(nproc --all)}

echo "cores=$(nproc --all) render_threads=$RENDER_THREADS"

# --osp:num-threads, not OSPRAY_NUM_THREADS: the image's %environment hard-sets
# that variable at container startup and discards anything exported out here.
apptainer exec $SIF ./build/bin/ospr_render \
    --osp:num-threads=$RENDER_THREADS \
    scenes/peel.json --out out/peel
