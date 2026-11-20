// demo_tag_types.java
//
// Set the evidence tag and origin address on each data type's Ghidra
// DESCRIPTION, which is where CLAUDE.md rule 5 keeps them and the only
// place demo_export_maps.java reads them from.
//
// It exists because the MCP has no tool for a type's description. Every
// other part of rule 5 can be done through a tool call; this one cannot,
// so without this script every type we create exports as src=untagged
// and structmap.py check reports it forever.
//
// THIS FILE OWNS THE DESCRIPTION OF EVERY TYPE LISTED IN IT (rule 7.2).
// Add a row here rather than setting a description by hand, so that
// re-running it is always safe and never reverts a tag somebody else
// set. It edits descriptions only - it never creates, resizes or
// deletes a type, and it leaves a type it does not list alone.
//
// The first line of a description is fixed and parsed:
//
//     [verified] origin=1000:7f5e  what the thing is
//
// Run it like the exporter, through switch_program first:
//
//   py -3.10 tools/gq.py call switch_program program=XCURSION_U.EXE
//   py -3.10 tools/gq.py call run_ghidra_script \
//            script_name=demo_tag_types.java program=XCURSION_U.EXE
//
// @category PCDEMOS

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeManager;

public class demo_tag_types extends GhidraScript {

    // program, type name, description. The program column is checked
    // against the one actually open, because a script does not get the
    // MCP's program selector (rule 7.1) and silently tagging the wrong
    // database's types would be the same class of mistake.
    private static final String[][] TAGS = {
        {"XCURSION_U.EXE", "Xcur_SinTable",
         "[verified] origin=1000:7f5e  a FLAT 640-dword sine table, "
         + "4-byte stride: 4095*sin(2*pi*k/512) to within 2.0, so 512 "
         + "steps, plus a 128-entry tail. The tail is why the COSINE is "
         + "the same table read 0x200 bytes on and never runs off the "
         + "end. CORRECTION: this was typed as 256 entries of a {sin, "
         + "sin_half} pair. That reading is not wrong about the height "
         + "fields, which index with an 8-byte stride and so see a "
         + "256-step sine, but Voxel_Render indexes with & 0x7fc at the "
         + "full 512 steps and the pair model gets its camera wrong by "
         + "a factor of two."},
        {"LITHIUM_U.EXE", "Lith_Shard",
         "[verified] origin=1000:2c78  one glass shard of the shatter: "
         + "0x30 bytes at 0x2ad2:0x4000 + 0x30n, g_ShardCount of them. "
         + "Three local corners at +/-16 (a 32x32 quad split into two "
         + "triangles), a 3D position whose z starts and ends at 0x100, "
         + "a rotation, their two rates, and the three source u,v the "
         + "shard is textured from. Field order verified against "
         + "Shatter_Step and Shatter_Draw, which between them touch "
         + "every offset. docs/lithium/effects/shatter.txt."},
        {"LITHIUM_U.EXE", "Lith_DustParticle",
         "[verified] origin=1000:6a23  one of the 1000 dust particles "
         + "at 0x2ad2:0x3820, 0xc bytes, the pool ending at 0x6700 "
         + "where the text mask begins. THREE 2D POINTS, NOT A "
         + "POSITION AND A VELOCITY: the drawn point eases toward the "
         + "mid point by a 32nd and the mid point toward the target "
         + "by an eighth, and only the target is ever written from "
         + "outside - by Dust_SampleMask, which picks it off a lit "
         + "pixel of the rendered text. Layout verified against "
         + "Tick_Dust, which touches every offset. "
         + "docs/lithium/effects/dust.txt."},
    };

    @Override
    protected void run() throws Exception {
        String prog = currentProgram.getName();
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        println("program: " + prog);

        int set = 0, skipped = 0, missing = 0;
        for (String[] row : TAGS) {
            if (!row[0].equals(prog)) {
                skipped++;
                continue;
            }
            // TAG EVERY MATCH, not the first. A name can belong to
            // more than one object - an array type and a typedef of it
            // both answer to Xcur_SinTable - and the exporter reads the
            // typedef while findDataTypes returned the array, so one
            // tag landed somewhere nothing reads and check kept
            // reporting the type as untagged.
            java.util.ArrayList<DataType> hits =
                new java.util.ArrayList<DataType>();
            dtm.findDataTypes(row[1], hits);
            if (hits.isEmpty()) {
                println("  MISSING  " + row[1]);
                missing++;
                continue;
            }
            for (DataType dt : hits) dt.setDescription(row[2]);
            println("  tagged   " + row[1] + "  (" + hits.size()
                    + " object(s) of that name)");
            set++;
        }

        // Both counts, because a script that prints only its own is how
        // a silent revert went unnoticed on a sister project (rule 7.2).
        int total = 0, untagged = 0;
        java.util.Iterator<DataType> it = dtm.getAllDataTypes();
        while (it.hasNext()) {
            DataType dt = it.next();
            String cat = dt.getCategoryPath().getPath();
            if (cat.startsWith("/demangler") || cat.startsWith("/DWARF")
                    || cat.startsWith("/PDB") || cat.startsWith("/ELF")
                    || cat.startsWith("/XBE") || cat.startsWith("/DOS")) {
                continue;
            }
            if (dt.getSourceArchive() != null
                    && !dt.getSourceArchive().equals(
                            dtm.getLocalSourceArchive())) {
                continue;
            }
            // Only the kinds the exporter lists can carry evidence.
            // Counting builtins here reported 3 untagged types on a
            // program whose only real type was tagged.
            if (!(dt instanceof ghidra.program.model.data.Composite
                  || dt instanceof ghidra.program.model.data.Enum
                  || dt instanceof ghidra.program.model.data.TypeDef)) {
                continue;
            }
            total++;
            String d = dt.getDescription();
            if (d == null || !d.trim().startsWith("[")) untagged++;
        }

        println(set + " tagged here, " + missing + " missing, "
                + skipped + " for another program");
        println(total + " type(s) of ours in this program, "
                + untagged + " still untagged");
    }

}
