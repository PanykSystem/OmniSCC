#include "RomStorageBuilder.hxx"
#include <cstring>
extern "C" {
#include "FlashAdapter.h"
}
uint8_t __not_in_flash_func(RomStorage::ReadAny)(uint32_t offset) const {
    return kind == RomStorageKind::Ram ? Read<RomStorageKind::Ram>(offset) : Read<RomStorageKind::Mapped>(offset);
}
static bool __attribute__((noinline, noclone)) __not_in_flash_func(VerifyRomPage)(uint32_t address, const uint8_t* scratch, UINT want) {
    const volatile uint8_t* physical=(const volatile uint8_t*)(uintptr_t)address;
    for (UINT j=0;j<want;++j) if (physical[j]!=scratch[j]) return false;
    return true;
}
MountResult BuildRomStorage(FIL& file, uint8_t* workspace, size_t capacity, RomStorage& out, bool forceMapped) {
    const uint64_t length = f_size(&file);
    uint32_t mask;
    if (!workspace || (uintptr_t(workspace)&3) || length > UINT32_MAX || !MakeRomMask(uint32_t(length),mask)) return MountResult::RomSize;
    const uint32_t size = uint32_t(length);
    if (file.fptr != 0) return MountResult::FileRead;
    if (!forceMapped && size <= 131072 && uint64_t(mask) + 1 <= capacity) {
        if (uint64_t(mask)+1 > capacity) return MountResult::RomSize;
        UINT got = 0;
        if (f_read(&file,workspace,size,&got) != FR_OK || got != size) return MountResult::FileRead;
        memset(workspace+size,0xff,mask+1-size);
        out = {RomStorageKind::Ram,size,mask,0,workspace,nullptr};
        return MountResult::Ok;
    }
    FATFS* fs = file.obj.fs;
    if (!fs || fs->n_fatent < 3 || !fs->csize || (fs->csize & (fs->csize-1))) return MountResult::FileRead;
    RomWorkspace layout;
    const uint32_t clusterCount = fs->n_fatent-2;
    if (!RomWorkspaceLayout(size,clusterCount,capacity,layout)) return MountResult::RomSize;
    auto pages = reinterpret_cast<uint32_t*>(workspace);
    auto scratch = workspace + layout.scratch;
    auto visited = workspace + layout.visited;
    memset(visited,0,(uint64_t(clusterCount)+7)/8);
    const uint32_t count = (size+511)/512;
    for (uint32_t i=0;i<count;++i) {
        const uint32_t offset = i*512;
        const UINT want = size-offset < 512 ? size-offset : 512;
        UINT got=0;
        if (f_read(&file,scratch,want,&got) != FR_OK || got != want) return MountResult::FileRead;
        const uint32_t cluster=file.clust;
        if (cluster < 2 || cluster >= fs->n_fatent) return MountResult::FileRead;
        const uint32_t sectorInCluster = i & (fs->csize-1);
        if (!sectorInCluster) {
            const uint32_t bit=cluster-2;
            if (visited[bit>>3] & (1u<<(bit&7))) return MountResult::FileRead;
            visited[bit>>3] |= 1u<<(bit&7);
        }
        const uint64_t lba=uint64_t(fs->database)+uint64_t(cluster-2)*fs->csize+sectorInCluster;
        uint32_t address;
        if (lba >= FAT_NUM_SECTORS || !flash_get_xip_page(uint32_t(lba),&address)) return MountResult::FileRead;
        if (!VerifyRomPage(address,scratch,want)) return MountResult::FileRead;
        // KUC checks the unmasked chip address against EOF before reading.
        // Keeping its final page in XIP avoids a 512-byte tail reservation at
        // the 8 MiB workspace boundary; ordinary mapper storage keeps the tail.
        if (want<512 && !forceMapped) {
            memset(workspace+layout.tail,0xff,512);
            memcpy(workspace+layout.tail,scratch,want);
            address=uint32_t(uintptr_t(workspace+layout.tail));
        }
        pages[i]=address;
    }
    out={RomStorageKind::Mapped,size,mask,count,nullptr,pages};
    return MountResult::Ok;
}
