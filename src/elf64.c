//
// Created by root on 2020/3/16.
//

#include <elf_64.h>

bool IsELF(const char *file) {
    unsigned char ident[EI_NIDENT];
    FILE *fd = fopen(file, "rb");
    if (!fd) {
        err_msg("Can not open file %s", file);
        return false;
    }
    int ret = fread(ident, 1, EI_NIDENT, fd);
    fclose(fd);
    if (ret != EI_NIDENT) {
        err_msg("Read ELF magic failed!");
        return false;
    }
    if (ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L' && ident[3] == 'F') {
        return true;
    } else {
        return false;
    }
}

void SetElfPath(Elf64 *elf64, const char *path) {
    int len = strlen(path);
    elf64->path = (char *) malloc(len);
    strcpy(elf64->path, path);
}

bool GetEhdr(Elf64 *elf64) {
    if (elf64->path == NULL) {
        err_msg("ELF file not set");
        return false;
    }
    FILE *fd = fopen(elf64->path, "rb");
    if (!fd) {
        err_msg("Can not open file %s", elf64->path);
        return false;
    }
    int ret = fread(&elf64->ehdr, 1, sizeof(Elf64_Ehdr), fd);
    fclose(fd);
    if (ret != sizeof(Elf64_Ehdr)) {
        err_msg("Read ELF Header failed");
        return false;
    }
    return true;
}

bool Getshstrtabhdr(Elf64 *elf64) {
    int offset = 0;
    if (elf64->path == NULL) {
        err_msg("ELF file not set");
        return false;
    }
    FILE *fd = fopen(elf64->path, "rb");
    if (!fd) {
        err_msg("Can not open file %s", elf64->path);
        return false;
    }
    offset = elf64->ehdr.e_shoff + elf64->ehdr.e_shentsize * elf64->ehdr.e_shstrndx;
    fseek(fd, offset, SEEK_SET);
    int ret = fread(&elf64->shstrtabhdr, 1, sizeof(Elf64_Shdr), fd);
    if (ret != sizeof(Elf64_Shdr)) {
        err_msg("Read Section Header Table failed");
        return false;
    }
    return true;
}

bool Getshstrtab(Elf64 *elf64) {
    if (elf64->path == NULL) {
        err_msg("ELF file not set");
        return false;
    }
    FILE *fd = fopen(elf64->path, "rb");
    if (!fd) {
        err_msg("Can not open file %s", elf64->path);
        return false;
    }
    elf64->shstrtab = (char *) malloc(elf64->shstrtabhdr.sh_size);
    fseek(fd, elf64->shstrtabhdr.sh_offset, SEEK_SET);
    int ret = fread(elf64->shstrtab, 1, elf64->shstrtabhdr.sh_size, fd);
    fclose(fd);
    if (ret != elf64->shstrtabhdr.sh_size) {
        err_msg("Read shstrtab Section failed");
        return false;
    }
    return true;
}

// Get orign file size
int GetFileSize(Elf64 *elf64) {
    if (!elf64->path) {
        err_msg("ELF file not set");
        return -1;
    }
    FILE *fd = fopen(elf64->path, "rb");
    if (!fd) {
        err_msg("Can not open file %s", elf64->path);
        return -1;
    }
    fseek(fd, 0, SEEK_END);
    elf64->size = ftell(fd);
    return elf64->size;
}

// Add a new section header at the end of file
bool AddSectionHeader(Elf64 *elf64) {
    if (elf64->path == NULL) {
        err_msg("ELF file not set");
        return false;
    }
    FILE *fd = fopen(elf64->path, "ab+");
    if (!fd) {
        err_msg("Can not open file %s", elf64->path);
        return false;
    }
    fseek(fd, 0, SEEK_END);

    Elf64_Shdr signSection;
    CreateSignSection(elf64, &signSection);
    int ret = fwrite(&signSection, 1, sizeof(Elf64_Shdr), fd);
    fclose(fd);
    log_msg("ret is %d", ret);
    if (ret != sizeof(Elf64_Shdr)) {
        err_msg("Write Sign Section Header Failded");
        return false;
    }
    return true;
}

// Init a new section header
bool CreateSignSection(Elf64 *elf64, Elf64_Shdr *signSection) {
    int shstrOffset = elf64->shstrtabhdr.sh_offset;
    signSection->sh_name = sizeof(Elf64_Shdr) + elf64->size - shstrOffset;
    signSection->sh_type = SHT_NOTE;
    signSection->sh_flags = SHF_ALLOC;
    signSection->sh_addr = elf64->size + sizeof(Elf64_Shdr) + 8;
    signSection->sh_offset = elf64->size + sizeof(Elf64_Shdr) + 8;
    signSection->sh_size = 256; // RSA sign length
    signSection->sh_link = 0;
    signSection->sh_info = 0;
    signSection->sh_addralign = 1;
    signSection->sh_entsize = 0;
    return true;
}

// Add section name ".sign" at the end of file
bool AddSectionName(Elf64 *elf64) {
    const char *sectionName = ".sign\0\0\0";
    if (elf64->path == NULL) {
        err_msg("ELF file not set");
        return false;
    }

    FILE *fd = fopen(elf64->path, "ab+");
    if (!fd) {
        err_msg("Can not open file %s", elf64->path);
        return false;
    }
    int ret = fwrite(sectionName, 1, 8, fd);
    fclose(fd);
    if (ret != 8) {
        err_msg("Write section name failed");
        return false;
    }
    ret = UpdateShstrtabSize(elf64);
    if (!ret)
        return false;
    ret = UpdateShnum(elf64);
    if (!ret)
        return false;
    return true;
}

bool UpdateShstrtabSize(Elf64 *elf64) {
    int offset = 0, size = 0;
    FILE *fd = fopen(elf64->path, "rb+");
    if (!fd) {
        err_msg("Can not open file %s", elf64->path);
        return false;
    }

    // offset to Section shstrtab's Header -> sh_size
    // 1. Go to shstrtab header item
    offset = elf64->ehdr.e_shoff + elf64->ehdr.e_shentsize * elf64->ehdr.e_shstrndx;
    // 2. sh_name + sh_type + sh_flags + sh_offset
    offset += sizeof(Elf64_Word) * 2 + sizeof(Elf64_Xword) + sizeof(Elf64_Addr) + sizeof(Elf64_Off);
    fseek(fd, offset, SEEK_SET);


    // end + section_header + name - shstrtab_offset
    size = elf64->size + sizeof(Elf64_Shdr) + 6 - elf64->shstrtabhdr.sh_offset;
    log_msg("Size of new size %d", size);
    int ret = fwrite(&size, 1, sizeof(size), fd);
    fclose(fd);
    if (ret != sizeof(size)) {
        err_msg("Write new section size failed");
        return false;
    }
    return true;
}

bool UpdateShnum(Elf64 *elf64) {
    int offset = 0;
    Elf64_Half newSize = elf64->ehdr.e_shnum + 1;
    FILE *fd = fopen(elf64->path, "rb+");
    if (!fd) {
        err_msg("Can not open file %s", elf64->path);
        return false;
    }

    offset = sizeof(Elf64_Ehdr) - sizeof(Elf64_Half) * 2;
    log_msg("Offset number of sections is %d(%p)", offset, offset);
    fseek(fd, offset, SEEK_SET);
    int ret = fwrite(&newSize, 1, sizeof(newSize), fd);
    fclose(fd);
    log_msg("ret is %d", ret);
    if (ret != sizeof(newSize)) {
        err_msg("Write new section number failed");
        return false;
    }
    return true;
}

bool HashText(Elf64 *elf64) {
    Elf64_Off sectionHeaderTable = elf64->ehdr.e_shoff;
    Elf64_Shdr tmp;
    int textOffset;
    char name[20];
    unsigned char buf[1];

    SHA_CTX ctx;
    SHA1_Init(&ctx);

    FILE *fd = fopen(elf64->path, "rb");
    if (!fd) {
        err_msg("Can not open file %s", elf64->path);
        return false;
    }
    fseek(fd, sectionHeaderTable, SEEK_SET);
    do {
        int ret = fread(&tmp, 1, sizeof(Elf64_Shdr), fd);
        if (ret != sizeof(Elf64_Shdr)) {
            err_msg("Read section header failed");
            return false;
        }
        strcpy(name, elf64->shstrtab + tmp.sh_name);
        log_msg("Section name is %s", name);
    } while (strcmp(name, ".text"));
    if (strcmp(name, ".text")) {
        err_msg("Not found .text section");
        return false;
    }
    textOffset = tmp.sh_offset;
    fseek(fd, textOffset, SEEK_SET);

    for (int i = 0; i < tmp.sh_size; i++) {
        int ret = fread(buf, 1, 1, fd);
        if (ret != 1) {
            err_msg("Read .text section failed");
            return false;
        }
        SHA1_Update(&ctx, buf, 1);
    }
    fclose(fd);
    SHA1_Final(elf64->digest, &ctx);
    return true;
}

void Destract(Elf64 *elf64) {
    if (elf64->path != NULL) {
        free(elf64->path);
    }
    if (elf64->shstrtab != NULL) {
        free(elf64->shstrtab);
    }
}


















