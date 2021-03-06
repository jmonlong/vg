/** \file gbwt_main.cpp
 *
 * Defines the "vg gbwt" subcommand, which wraps up access for commands we'd otherwise find
 * in the gbwt submodule.  */

#include <omp.h>
#include <unistd.h>
#include <getopt.h>

#include <list>
#include <fstream>

#include "subcommand.hpp"

#include "../xg.hpp"
#include "../gbwt_helper.hpp"
#include "../stream/vpkg.hpp"

using namespace std;
using namespace vg;
using namespace vg::subcommand;

#include <unistd.h>


void help_gbwt(char** argv) {
    cerr << "usage: " << argv[0] << " [options] [args]" << endl
         << "Manipulate GBWTs." << endl
         << "merging (use deps/gbwt/merge_gbwt for more options):" << endl
         << "    -m, --merge            merge the GBWT files from the input args and write to output" << endl
         << "    -o, --output X         write output GBWT to X" << endl
         << "    -f, --fast             fast merging algorithm (node ids must not overlap; implies -m)" << endl
         << "    -p, --progress         show progress and statistics" << endl
         << "threads:" << endl
         << "    -c, --count-threads    print the number of threads" << endl
         << "    -e, --extract FILE     extract threads in SDSL format to FILE" << endl
         << "    -r, --remove-thread N  remove the thread with identifier N (may repeat; use -o to change output)" << endl
         << "metadata (use deps/gbwt/metadata to modify):" << endl
         << "    -M, --metadata         print all metadata" << endl
         << "    -C, --contigs          print the number of contigs" << endl
         << "    -H, --haplotypes       print the number of haplotypes" << endl
         << "    -S, --samples          print the number of samples" << endl
         << endl;
}

int main_gbwt(int argc, char** argv)
{
    if (argc == 2) {
        help_gbwt(argv);
        return 1;
    }

    bool merge = false;
    bool fast_merging = false;
    bool show_progress = false;
    bool count_threads = false;
    bool metadata = false, contigs = false, haplotypes = false, samples = false;
    bool load_index = false;
    string gbwt_output, thread_output;
    std::vector<gbwt::size_type> to_remove;

    int c;
    optind = 2; // force optind past command positional argument    
    while (true) {
        static struct option long_options[] =
            {
                // Merging
                {"merge", no_argument, 0, 'm'},
                {"output", required_argument, 0, 'o'},
                {"fast", no_argument, 0, 'f'},
                {"progress",  no_argument, 0, 'p'},

                // Threads
                {"count-threads", no_argument, 0, 'c'},
                {"extract", required_argument, 0, 'e'},
                {"remove-thread", required_argument, 0, 'r'},

                // Metadata
                {"metadata", no_argument, 0, 'M'},
                {"contigs", no_argument, 0, 'C'},
                {"haplotypes", no_argument, 0, 'H'},
                {"samples", no_argument, 0, 'S'},

                {"help", no_argument, 0, 'h'},
                {0, 0, 0, 0}
            };

        int option_index = 0;
        c = getopt_long(argc, argv, "mo:fpce:r:MCHSh?", long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c)
        {
        // Merging
        case 'm':
            merge = true;
            break;
        case 'o':
            gbwt_output = optarg;
            break;
        case 'f':
            fast_merging = true;
            merge = true;
            break;
        case 'p':
            show_progress = true;
            break;

        // Threads
        case 'c':
            count_threads = true;
            load_index = true;
            break;
        case 'e':
            thread_output = optarg;
            load_index = true;
            break;
        case 'r':
            to_remove.push_back(parse<size_t>(optarg));
            break;

        // Metadata
        case 'M':
            metadata = true;
            load_index = true;
            break;
        case 'C':
            contigs = true;
            load_index = true;
            break;
        case 'H':
            haplotypes = true;
            load_index = true;
            break;
        case 'S':
            samples = true;
            load_index = true;
            break;

        case 'h':
        case '?':
            /* getopt_long already printed an error message. */
            help_gbwt(argv);
            exit(1);
            break;

        default:
            abort ();
        }
    }

    gbwt::Verbosity::set(gbwt::Verbosity::SILENT);

    if (merge) {

        // Ugly hack here. GBWT prints to stdout, and we want to direct it to stderr.
        std::streambuf* cout_buf = cout.rdbuf();
        cout.rdbuf(cerr.rdbuf());

        size_t input_files = argc - optind;
        size_t total_inserted = 0;
        if (input_files <= 1) {
            cerr << "error: [vg gbwt] at least two input gbwt files required to merge" << endl;
            exit(1);
        }
        if (gbwt_output.empty()) {
            cerr << "error: [vg gbwt] output file must be specified with -o" << endl;
            exit(1);
            cerr << "[vg gbwt] error: at least two input gbwt files required to merge" << endl;
        }
        if (gbwt_output.empty()) {
            cerr << "[vg gbwt] error: output file must be specified with -o" << endl;
            exit(1);
        }
        if (show_progress) {
            gbwt::printHeader("Algorithm"); cout << (fast_merging ? "fast" : "insert") << endl;
            gbwt::printHeader("Input files"); cout << input_files << endl;
            gbwt::printHeader("Output name"); cout << gbwt_output << endl;
            cout << endl;
        }

        double start = gbwt::readTimer();

        if(fast_merging)
        {
            vector<gbwt::GBWT> indexes(argc - optind);
            for(int i = optind; i < argc; i++)
            {
                string input_name = argv[i];
                
                // Try loading the GBWT
                unique_ptr<gbwt::GBWT> loaded = stream::VPKG::load_one<gbwt::GBWT>(input_name);
                if (loaded.get() == nullptr) {
                    cerr << "error: [vg gbwt] could not load GBWT " << input_name << endl;
                    exit(1);
                }
                
                // Move out of the unique_ptr and into the vector that the GBWT library needs.
                indexes[i - optind] = std::move(*loaded);
                
                if (show_progress) {
                    gbwt::printStatistics(indexes[i - optind], input_name);
                }
                total_inserted += indexes[i - optind].size();
            }
            
            // Merge the GBWT
            gbwt::GBWT merged(indexes);
            
            // Save to a file in VPKG-encapsulated format.
            stream::VPKG::save(merged, gbwt_output);
            
            if (show_progress) {
                gbwt::printStatistics(merged, gbwt_output);
            }
        }
        else
        {
            unique_ptr<gbwt::DynamicGBWT> index;
            {
                string input_name = argv[optind];
                
                // Try to load the first GBWT as a dynamic one
                index = stream::VPKG::load_one<gbwt::DynamicGBWT>(input_name);
                
                if (index.get() == nullptr) {
                    cerr << "error: [vg gbwt] could not load dynamic GBWT " << input_name << endl;
                    exit(1);
                }
                
                if (show_progress) {
                    gbwt::printStatistics(*index, input_name);
                }
            }
            for (int curr = optind + 1; curr < argc; curr++)
            {
                string input_name = argv[curr];
                unique_ptr<gbwt::GBWT> next = stream::VPKG::load_one<gbwt::GBWT>(input_name);
                
                if (next.get() == nullptr) {
                    cerr << "error: [vg gbwt]: could not load GBWT " << input_name << endl;
                    exit(1);
                }
                
                if (show_progress) {
                    gbwt::printStatistics(*next, input_name);
                }
                index->merge(*next);
                total_inserted += next->size();
            }
            
            // Save to a file in VPKG-encapsulated format.
            stream::VPKG::save(*index, gbwt_output);
            
            if (show_progress) { 
                gbwt::printStatistics(*index, gbwt_output);
            }
        }
        
        double seconds = gbwt::readTimer() - start;

        if (show_progress) {

            cout << "Inserted " << total_inserted << " nodes in " << seconds << " seconds ("
                      << (total_inserted / seconds) << " nodes/second)" << endl;
            cout << "Memory usage " << gbwt::inGigabytes(gbwt::memoryUsage()) << " GB" << endl;
            cout << endl;
        }

        // Revert the hack.
        cout.rdbuf(cout_buf);
    }

    // Remove threads before extracting or counting them.
    if (!to_remove.empty()) {
        if (optind + 1 != argc) {
            cerr << "error: [vg gbwt] non-merge options require one input file" << endl;
            return 1;
        }
        
        // Try to load the GBWT as a dynamic one
        unique_ptr<gbwt::DynamicGBWT> index = stream::VPKG::load_one<gbwt::DynamicGBWT>(argv[optind]);
        
        if (index.get() == nullptr) {
            cerr << "error: [vg gbwt]: could not load dynamic GBWT " << argv[optind] << endl;
            exit(1);
        }
        
        gbwt::size_type total_length = index->remove(to_remove);
        if (total_length > 0) {
            std::string output = (gbwt_output.empty() ? argv[optind] : gbwt_output);
            
            // Save as an encapsulated file
            stream::VPKG::save(*index, output);
        }
    }

    // Other non-merge options.
    if (load_index) {
        if (optind + 1 != argc) {
            cerr << "error: [vg gbwt] non-merge options require one input file" << endl;
            return 1;
        }
        unique_ptr<gbwt::GBWT> index = stream::VPKG::load_one<gbwt::GBWT>(argv[optind]);

        if (index.get() == nullptr) {
            cerr << "error: [vg gbwt]: could not load GBWT " << argv[optind] << endl;
            exit(1);
        }

        // Extract threads in SDSL format.
        if (!thread_output.empty()) {
            gbwt::size_type node_width = gbwt::bit_length(index->sigma() - 1);
            gbwt::text_buffer_type out(thread_output, std::ios::out, gbwt::MEGABYTE, node_width);
            for (gbwt::size_type id = 0; id < index->sequences(); id += 2) { // Ignore reverse complements.
                gbwt::vector_type sequence = index->extract(id);
                for (auto node : sequence) {
                    out.push_back(node);
                }
                out.push_back(gbwt::ENDMARKER);
            }
            out.close();
        }

        // There are two sequences for each thread.
        if (count_threads) {
            cout << (index->sequences() / 2) << endl;
        }

        if (metadata) {
            if (index->hasMetadata()) {
                gbwt::operator<<(cout, index->metadata) << endl;
            }
        }

        if (contigs) {
            if (index->hasMetadata()) {
                cout << index->metadata.contigs() << endl;
            } else {
                cout << -1 << endl;
            }
        }

        if (haplotypes) {
            if (index->hasMetadata()) {
                cout << index->metadata.haplotypes() << endl;
            } else {
                cout << -1 << endl;
            }
        }

        if (samples) {
            if (index->hasMetadata()) {
                cout << index->metadata.samples() << endl;
            } else {
                cout << -1 << endl;
            }
        }
    }

    return 0;
}


// Register subcommand
static Subcommand vg_gbwt("gbwt", "Manipulate GBWTs", main_gbwt);

