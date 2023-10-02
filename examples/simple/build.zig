const std = @import("std");

var general_purpose_allocator = std.heap.GeneralPurposeAllocator(.{}){};
const gpa = general_purpose_allocator.allocator();

fn fmtPrint(comptime fmt: []const u8, args: anytype) []const u8 {
    return std.fmt.allocPrint(gpa, fmt, args) catch "Could not format print!";
}

const MicrokitBoard = enum {
    qemu_arm_virt,
    qemu_riscv_virt,
    odroidc4
};

const Target = struct {
    board: MicrokitBoard,
    zig_target: std.zig.CrossTarget,
};

const targets = [_]Target {
    .{
        .board = MicrokitBoard.qemu_arm_virt,
        .zig_target = std.zig.CrossTarget{
            .cpu_arch = .aarch64,
            .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a53 },
            .os_tag = .freestanding,
            .abi = .none,
        },
    },
   .{
        .board = MicrokitBoard.qemu_riscv_virt,
        .zig_target = std.zig.CrossTarget{
            .cpu_arch = .riscv64,
            // @ivanv: the generic rv64 does not include "imac"
            .cpu_model = .{ .explicit = &std.Target.riscv.cpu.generic_rv64 },
            .os_tag = .freestanding,
            .abi = .none,
        },
    },
    .{
        .board = MicrokitBoard.odroidc4,
        .zig_target = std.zig.CrossTarget{
            .cpu_arch = .aarch64,
            .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a55 },
            .os_tag = .freestanding,
            .abi = .none,
        },
    }
};

fn findTarget(board: MicrokitBoard) std.zig.CrossTarget {
    for (targets) |target| {
        if (board == target.board) {
            return target.zig_target;
        }
    }

    std.log.err("Board '{}' is not supported\n", .{ board });
    std.os.exit(1);
}

const ConfigOptions = enum {
    debug,
    release,
    benchmark
};

pub fn build(b: *std.Build) void {
    // @ivanv: need to somehow relate Microkit config with this optimisation level?
    const optimize = b.standardOptimizeOption(.{});

    // Getting the path to the Microkit SDK before doing anything else
    const microkit_sdk_arg = b.option([]const u8, "sdk", "Path to Microkit SDK");
    if (microkit_sdk_arg == null) {
        std.log.err("Missing -Dsdk=/path/to/sdk argument being passed\n", .{});
        std.os.exit(1);
    }
    const microkit_sdk = microkit_sdk_arg.?;

    const microkit_config_option = b.option(ConfigOptions, "config", "Microkit config to build for")
                                 orelse ConfigOptions.debug;
    const microkit_config = @tagName(microkit_config_option);

    // Get the Microkit SDK board we want to target
    const microkit_board_option = b.option(MicrokitBoard, "board", "Microkit board to target");

    if (microkit_board_option == null) {
        std.log.err("Missing -Dboard=<BOARD> argument being passed\n", .{});
        std.os.exit(1);
    }
    const target = findTarget(microkit_board_option.?);
    const microkit_board = @tagName(microkit_board_option.?);

    // Since we are relying on Zig to produce the final ELF, it needs to do the
    // linking step as well.
    const sdk_board_dir = fmtPrint("{s}/board/{s}/{s}", .{ microkit_sdk, microkit_board, microkit_config });
    const sdk_tool = fmtPrint("{s}/bin/microkit", .{ microkit_sdk });
    const libmicrokit = fmtPrint("{s}/lib/libmicrokit.a", .{ sdk_board_dir });
    const libmicrokit_linker_script = fmtPrint("{s}/lib/microkit.ld", .{ sdk_board_dir });
    const sdk_board_include_dir = fmtPrint("{s}/include", .{ sdk_board_dir });

    const libvmm = b.addStaticLibrary(.{
        .name = "vmm",
        .target = target,
        .optimize = optimize,
    });

    const libvmm_tools = "../../tools/";
    const libvmm_src = "../../src/";
    const arch_str = switch (target.cpu_arch.?) {
        .aarch64 => "aarch64",
        // libvmm uses the same directory for both 32-bit and 64-bit RISC-V
        .riscv64, .riscv32 => "riscv",
        else => {
            std.log.err("Unknown CPU target architecture: {s}\n", .{ @tagName(target.cpu_arch.?) });
            std.os.exit(1);
        }
    };
    const libvmm_src_arch = fmtPrint("{s}/arch/{s}", .{ libvmm_src, arch_str });

    const libvmm_aarch64_files: []const []const u8 = &.{
        libvmm_src ++ "guest.c",
        libvmm_src ++ "util/util.c",
        libvmm_src ++ "util/printf.c",
        libvmm_src ++ "arch/aarch64/vgic/vgic.c",
        libvmm_src ++ "arch/aarch64/vgic/vgic_v2.c",
        libvmm_src ++ "arch/aarch64/fault.c",
        libvmm_src ++ "arch/aarch64/psci.c",
        libvmm_src ++ "arch/aarch64/smc.c",
        libvmm_src ++ "arch/aarch64/virq.c",
        libvmm_src ++ "arch/aarch64/linux.c",
        libvmm_src ++ "arch/aarch64/tcb.c",
        libvmm_src ++ "arch/aarch64/vcpu.c",
    };
    const libvmm_riscv_files: []const []const u8 = &.{
        libvmm_src ++ "guest.c",
        libvmm_src ++ "util/util.c",
        libvmm_src ++ "util/printf.c",
        libvmm_src ++ "arch/riscv/fault.c",
        libvmm_src ++ "arch/riscv/plic.c",
        libvmm_src ++ "arch/riscv/virq.c",
        libvmm_src ++ "arch/riscv/linux.c",
        libvmm_src ++ "arch/riscv/tcb.c",
        libvmm_src ++ "arch/riscv/vcpu.c",
    };

    const cpu_arch = target.cpu_arch.?;
    const libvmm_source_files = blk: {
        if (cpu_arch == .aarch64) {
            break :blk libvmm_aarch64_files;
        } else if (cpu_arch == .riscv64 or cpu_arch == .riscv32) {
            break :blk libvmm_riscv_files;
        } else {
            unreachable;
        }
    };

    libvmm.addCSourceFiles(libvmm_source_files, &.{
        "-Wall",
        "-Werror",
        "-Wno-unused-function",
        // "-mstrict-align", @ivanv: only for AArch64?
        fmtPrint("-DBOARD_{s}", .{ microkit_board }), // @ivanv: shouldn't be needed as the library should not depend on the board
    });

    libvmm.addIncludePath(.{ .path = libvmm_src });
    libvmm.addIncludePath(.{ .path = libvmm_src ++ "util/" });
    libvmm.addIncludePath(.{ .path = libvmm_src_arch });
    libvmm.addIncludePath(.{ .path = sdk_board_include_dir });

    b.installArtifact(libvmm);

    const exe = b.addExecutable(.{
        .name = "vmm.elf",
        .target = target,
        .optimize = optimize,
    });

    // For actually compiling the DTS into a DTB
    const dts_path = fmtPrint("board/{s}/linux.dts", .{ microkit_board });
    const dtb_image_path = "linux.dtb";
    const dtc_command = b.addSystemCommand(&[_][]const u8{
        "dtc", "-I", "dts", "-O", "dtb", dts_path, "-o", dtb_image_path
    });

    // Add microkit.h to be used by the API wrapper.
    exe.addIncludePath(.{ .path = sdk_board_include_dir });
    // The VMM code will include 
    exe.addIncludePath(.{ .path = libvmm_src });
    exe.addIncludePath(.{ .path = libvmm_src_arch });
    // Add the static library that provides each protection domain's entry
    // point (`main()`), which runs the main handler loop.
    exe.addObjectFile(.{ .path = libmicrokit });
    exe.linkLibrary(libvmm);
    // Specify the linker script, this is necessary to set the ELF entry point address.
    exe.setLinkerScriptPath(.{ .path = libmicrokit_linker_script });

    exe.addCSourceFiles(&.{"vmm.c"}, &.{
        "-Wall",
        "-Werror",
        "-Wno-unused-function",
        // "-mstrict-align", @ivanv: only for AArch64?
        fmtPrint("-DBOARD_{s}", .{ microkit_board }),
    });

    const guest_images = b.addObject(.{
        .name = "guest_images",
        .target = target,
        .optimize = optimize,
    });
    // We need to produce the DTB from the DTS before doing anything to produce guest_images
    guest_images.step.dependOn(&dtc_command.step);

    const linux_image_path = fmtPrint("board/{s}/linux", .{ microkit_board });
    const kernel_image_arg = fmtPrint("-DGUEST_KERNEL_IMAGE_PATH=\"{s}\"", .{ linux_image_path });

    const initrd_image_path = fmtPrint("board/{s}/rootfs.cpio.gz", .{ microkit_board });
    const initrd_image_arg = fmtPrint("-DGUEST_INITRD_IMAGE_PATH=\"{s}\"", .{ initrd_image_path });
    const dtb_image_arg = fmtPrint("-DGUEST_DTB_IMAGE_PATH=\"{s}\"", .{ dtb_image_path });
    guest_images.addCSourceFiles(&.{ libvmm_tools ++ "package_guest_images.S" }, &.{
        kernel_image_arg,
        dtb_image_arg,
        initrd_image_arg,
        "-x",
        "assembler-with-cpp",
    });

    exe.addObject(guest_images);
    b.installArtifact(exe);

    const system_description_path = fmtPrint("board/{s}/simple.system", .{ microkit_board });
    const final_image_dest = b.getInstallPath(.bin, "./loader.img");
    const microkit_tool_cmd = b.addSystemCommand(&[_][]const u8{
       sdk_tool,
       system_description_path,
       "--search-path",
       b.getInstallPath(.bin, ""),
       "--board",
       microkit_board,
       "--config",
       microkit_config,
       "-o",
       final_image_dest,
       "-r",
       b.getInstallPath(.prefix, "./report.txt")
    });
    // Running the Microkit tool depends on 
    microkit_tool_cmd.step.dependOn(b.getInstallStep());
    // Add the "microkit" step, and make it the default step when we execute `zig build`>
    const microkit_step = b.step("microkit", "Compile and build the final bootable image");
    microkit_step.dependOn(&microkit_tool_cmd.step);
    b.default_step = microkit_step;

    // This is setting up a `qemu` command for running the system via QEMU,
    // which we only want to do when we have a board that we can actually simulate.
    if (std.mem.eql(u8, microkit_board, "qemu_arm_virt")) {
        const loader_arg = fmtPrint("loader,file={s},addr=0x70000000,cpu-num=0", .{ final_image_dest });
        const qemu_cmd = b.addSystemCommand(&[_][]const u8{
            "qemu-system-aarch64",
            "-machine",
            "virt,virtualization=on,highmem=off,secure=off",
            "-cpu",
            "cortex-a53",
            "-serial",
            "mon:stdio",
            "-device",
            loader_arg,
            "-m",
            "2G",
            "-nographic",
        });
        qemu_cmd.step.dependOn(b.default_step);
        const simulate_step = b.step("qemu", "Simulate the image via QEMU");
        simulate_step.dependOn(&qemu_cmd.step);
    } else if (std.mem.eql(u8, microkit_board, "qemu_riscv_virt")) {
        // @ivanv: this assumes rv64
        const qemu_cmd = b.addSystemCommand(&[_][]const u8{
            "qemu-system-riscv64",
            "-machine",
            "virt",
            "-serial",
            "mon:stdio",
            "-kernel",
            final_image_dest,
            "-m",
            "3G",
            "-nographic",
        });
        qemu_cmd.step.dependOn(b.default_step);
        const simulate_step = b.step("qemu", "Simulate the image via QEMU");
        simulate_step.dependOn(&qemu_cmd.step);
    }
}
