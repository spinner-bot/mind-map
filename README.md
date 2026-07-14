# Mind Map - Tree Structure Multi-Format Conversion Tool

I've always regretted that there's no tool out there that automatically scans my thoughts and generates a .mind file the instant I hit "Save". So I built this project to make things just a little better -- and maybe it can help you too.

## Overview

A C language tool for multi-format conversion and visualization of tree-structured information (mind maps, outlines, study notes, presentation structures). Phase 1 provides a simple GUI for file import, format conversion, and batch processing.

## Supported Formats (Phase 1)

| Format | Import | Export | Notes |
|--------|--------|--------|-------|
| JSON   | Yes    | Yes    | Lists only; dict→list with warning; index-0 branch info mode |
| TXT    | Yes    | Yes    | Numbered outline format (e.g., "1.", "1.2.", "2.") |
| MD     | Yes    | Yes    | Heading-based tree structure |

## Building

### Requirements
- GCC/MinGW (C11 standard)
- Windows with Win32 API

### Build Instructions
```bash
make all      # Build mind_map.exe
make run      # Build and run
make clean    # Remove build artifacts
```

## Project Structure

```
mind_map/
├── include/       # Header files (.h)
├── src/           # Source files (.c only)
├── obj/           # Build artifacts
├── Makefile       # Build system
└── README.md
```

## License

This project is for personal and educational use.
