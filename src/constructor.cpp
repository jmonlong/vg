/**
 * \file
 * constructor.cpp: contains implementations for vg construction functions.
 */


#include "vg.hpp"
#include "constructor.hpp"

#include <cstdlib>
#include <set>
#include <tuple>
#include <list>
#include <algorithm>
#include <memory>

//#define debug

namespace vg {

    using namespace std;

    void Constructor::trim_to_variable(vector<list<vcflib::VariantAllele>>& parsed_alleles) {

#ifdef debug
        cerr << "Before trimming to variable region:" << endl;
        for (auto& allele : parsed_alleles) {
            cerr << "Allele:" << endl;

            for (auto& edit : allele) {
                cerr << "\tHave " << edit.ref << " -> " << edit.alt << " @ " << edit.position << endl; 
            }
        }
#endif

        // Return the length of perfect match common to all alleles at the left or
        // the right (based on the front parameter). Only looks at the first or last
        // edit in each allele
        auto get_match_count = [&](bool front) -> size_t { 
            // Start with the max possible value
            size_t match_count = numeric_limits<size_t>::max();
            for (auto& allele : parsed_alleles) {
                // Go throught the alleles
                if (allele.empty()) {
                    // No shared ref match possible with an empty allele
                    return 0;
                }
                // Find the edit on the appropriate edge of this alt
                auto& edit = front ? allele.front() : allele.back();
                if (edit.ref != edit.alt) {
                    // This alt has a non-match edit on its edge
                    return 0;
                }

                // Otherwise we have a leading or trailing match so mix in its length
                match_count = min(match_count, edit.ref.size());
            }
            if (match_count == numeric_limits<size_t>::max()) {
                // If nobody exists we have 0 shared matches.
                return 0;
            }
            return match_count;
        };

        for(size_t front_match_count = get_match_count(true); front_match_count > 0; front_match_count = get_match_count(true)) {
            // While we have shared matches at the front
            
#ifdef debug
            cerr << "Edits at the front share " << front_match_count << " match bases and need to be trimmed down" << endl;
#endif
            
            for (auto& allele : parsed_alleles) {
                // Trim each allele
                if (allele.front().ref.size() > front_match_count) {
                    // This perfect match needs to be made shorter
                    auto new_match_string = allele.front().ref.substr(front_match_count);
#ifdef debug
                    cerr << "Trim " << allele.front().ref << " to " << new_match_string
                        << " @ " << allele.front().position << endl;
#endif
                    allele.front().ref = new_match_string;
                    allele.front().alt = new_match_string;
                    
                    // Since we're trimming off the front we need to bump the position up.
                    allele.front().position += front_match_count;
                } else {
                    // This perfect match can be completely eliminated
#ifdef debug
                    cerr << "Drop " << allele.front().ref << " -> " << allele.front().alt
                        << " @ " << allele.front().position << endl;
#endif
                    allele.pop_front();
                }
            }
        }

        for(size_t back_match_count = get_match_count(false); back_match_count > 0; back_match_count = get_match_count(false)) {
            // While we have shared matches at the back
            
#ifdef debug
            cerr << "Edits at the back share " << back_match_count << " match bases and need to be trimmed down" << endl;
#endif
            
            for (auto& allele : parsed_alleles) {
                // Trim each allele
                if (allele.back().ref.size() > back_match_count) {
                    // This perfect match needs to be made shorter
                    auto new_match_string = allele.back().ref.substr(back_match_count);
#ifdef debug
                    cerr << "Trim " << allele.back().ref << " to " << new_match_string
                        << " @ " << allele.back().position << endl;
#endif
                    allele.back().ref = new_match_string;
                    allele.back().alt = new_match_string;
                } else {
                    // This perfect match can be completely eliminated
                    allele.pop_back();

#ifdef debug
                    cerr << "Drop " << allele.back().ref << " -> " << allele.back().alt
                        << " @ " << allele.back().position << endl; 
#endif

                }
            }
        }

#ifdef debug
        cerr << "After trimming to variable region:" << endl;
        for (auto& allele : parsed_alleles) {
            cerr << "Allele: " << endl;
            for (auto& edit : allele) {
                cerr << "\tKept " << edit.ref << " -> " << edit.alt << " @ " << edit.position << endl; 
            }
        }
#endif

    }

    void Constructor::condense_edits(list<vcflib::VariantAllele>& parsed_allele) {
        for(auto i = parsed_allele.begin(); i != parsed_allele.end(); ++i) {
            // Scan through the edits in the alt
            if (i->ref == i->alt) {
                // We can merge into this edit
                auto next = i;
                ++next;

                // We'll use a string stream to generate the combined string
                stringstream combined;
                combined << i->ref;

                while (next != parsed_allele.end() && next->ref == next->alt) {
                    // Glom up all the subsequent matches and remove their nodes.
                    combined << next->ref;
                    next = parsed_allele.erase(next);
                }

                // Put the finished string into the node that led the run
                i->ref = combined.str();
                i->alt = combined.str();
            }
        }
    }

    pair<int64_t, int64_t> Constructor::get_bounds(vcflib::Variant var){
        int64_t start = numeric_limits<int64_t>::max();

        start = min(start, (int64_t) var.zeroBasedPosition());
        int64_t end = -1;

        
        end = var.getMaxReferenceLength();
        
        return std::make_pair( start, end);
    }


    pair<int64_t, int64_t> Constructor::get_bounds(const vector<list<vcflib::VariantAllele>>& trimmed_variant) {

        // We track the variable site bounds through all the alts
        int64_t variable_start = numeric_limits<int64_t>::max();
        int64_t variable_stop = -1;

        for (auto& trimmed_parts : trimmed_variant) {
            // For every variable core of an alt (which may be empty)
            if (!trimmed_parts.empty()) {
                // We have at least one valid non-match edit on this alt. Expand the range.
                variable_start = min(variable_start, (int64_t) trimmed_parts.front().position - 1);
                variable_stop = max(variable_stop, (int64_t) (trimmed_parts.back().position - 1 + trimmed_parts.back().ref.size() - 1));
            }
        }

        #ifdef debug
        cerr << "Edits for variant run " << variable_start << " through " << variable_stop
            << " ref length " << (variable_stop - variable_start + 1) << endl;
        #endif

        return make_pair(variable_start, variable_stop);
    }

    ConstructedChunk Constructor::construct_chunk(string reference_sequence, string reference_path_name,
        vector<vcflib::Variant> variants, size_t chunk_offset) const {
            
        #ifdef debug
        cerr << "constructing chunk " << reference_path_name << ":" << chunk_offset << " length " << reference_sequence.size() << endl;
        #endif
        
        // Make sure the input sequence is upper-case
        string uppercase_sequence = toUppercase(reference_sequence);
        
        if (uppercase_sequence != reference_sequence && warn_on_lowercase) {
            #pragma omp critical (cerr)
            {
                // Note that the pragma also protects this mutable map that we update
                if (!warned_sequences.count(reference_path_name)) {
                    // We haven't warned about this sequence yet
                    cerr << "warning:[vg::Constructor] Lowercase characters found in "
                        << reference_path_name << "; coercing to uppercase." << endl;
                    warned_sequences.insert(reference_path_name);
                }    
            }
        }
        swap(reference_sequence, uppercase_sequence);

        // Construct a chunk for this sequence with these variants.
        ConstructedChunk to_return;

        // We need as path to add reference mappings to
        Path* ref_path = to_return.graph.add_path();
        ref_path->set_name(reference_path_name);

        // We use this to keep track of what the next unused base, if any, in the
        // reference is.
        size_t reference_cursor = 0;

        // We use this to number nodes. All chunks number nodes starting from 1.
        id_t next_id = 1;

        // We remember nodes ending at these reference positions that haven't yet
        // all been wired up yet. These are on-the-end and not past-the-end
        // positions, so they are start + length - 1.
        map<size_t, set<id_t>> nodes_ending_at;
        // And nodes starting at these reference positions that haven't yet all been
        // wired up. 
        map<size_t, set<id_t>> nodes_starting_at;

        // We don't want to wire inserts to each other, so we have a set of all
        // insert endpoints.
        set<id_t> inserts;

        // We need to wire up our inversions super specially.
        map<size_t, set<id_t> > inversion_starts;
        map<size_t, set<id_t> > inversion_ends;

        // Here we remember deletions that end at paritcular positions in the
        // reference, which are the positions of the last deleted bases. We map from
        // last deleted base to last non-deleted base before the deletion, so we can
        // go look up nodes ending there. Note that these can map to -1.
        map<size_t, set<int64_t>> deletions_ending_at;

        // We also need to track all points at which deletions start, so we can
        // search for the next one when deciding where to break the reference.
        set<int64_t> deletion_starts;

        // We need to track where the alt paths of deletions start/end (and maintain their ids)
        // so that we can put them in the graph.
        map<int64_t, string> deletion_start_to_alt_name;

        // We use this to get the next variant
        auto next_variant = variants.begin();

        // We're going to clump overlapping variants together.
        vector<vcflib::Variant*> clump;
        // And we need to rember the highest past-the-end base of anything in the
        // clump, to catch all the overlaps.
        size_t clump_end = 0;

        // We use this to remember path ranks. It will initialize to 0 for new
        // paths.
        map<Path*, size_t> max_rank;

        // We have a utility function to tack a full length perfect match onto a
        // path. We need the node so we can get its length.
        // Automatically fills in rank, starting from 1.
        auto add_match = [&](Path* path, Node* node) {
            #ifdef debug
            cerr << "Add node " << node->id() << " to path " << path->name() << endl;
            #endif
        
            // Make a mapping for it
            auto* mapping = path->add_mapping();
            mapping->mutable_position()->set_node_id(node->id());

            // Set the rank to the next available rank in the path.
            mapping->set_rank(++max_rank[path]);

            // Make it a perfect match explicitly
            auto* match_edit = mapping->add_edit();
            match_edit->set_from_length(node->sequence().size());
            match_edit->set_to_length(node->sequence().size());
        };

        // Given a string, turn it into nodes of the max node size or smaller, and
        // add them to the graph. Return pointers to the resulting nodes in the
        // graph, in order.
        auto create_nodes = [&](const string& sequence) -> vector<Node*> {

            // How big should each node try to be?
            size_t piece_size;

            if(greedy_pieces) {
                // Make pieces as big as possible.
                piece_size = max_node_size;
            } else {

                // Let's try to divide into evenly-sized pieces
                size_t piece_count = sequence.size() / max_node_size;
                if(piece_count > 1) {
                    piece_size = min(max_node_size, max(sequence.size() / piece_count, (size_t) 1));
                } else {
                    piece_size = max_node_size;
                }

                // Remember we may have a partial piece at the end.
            }

            // We'll fill this in with created nodes
            vector<Node*> created;

            // We keep a cursor to the next non-made-into-a-node base
            size_t cursor = 0;

            while (cursor < sequence.size()) {
                // There's still sequence to do, so bite off a piece
                size_t next_node_size = std::min(piece_size, sequence.size() - cursor);
                string node_sequence = sequence.substr(cursor, next_node_size);

                // Make a node
                auto* node = to_return.graph.add_node();
                node->set_id(next_id++);
                node->set_sequence(node_sequence);

                if (!created.empty()) {
                    // We need to link to the previous node in this run of sequence
                    auto* edge = to_return.graph.add_edge();
                    edge->set_from(created.back()->id());
                    edge->set_to(node->id());
                }

                // Tack the new node on the end of the list
                created.push_back(node);

                // Advance the cursor since we made this node
                cursor += next_node_size;
            }

            return created;
        };

        // We have a function to emit reference nodes from wherever the current
        // cursor position is up to the given position, advancing the cursor. The
        // target position must be <= the length of the reference. This function
        // adds the nodes to the starting and ending position indexes.
        auto add_reference_nodes_until = [&](size_t target_position) {

            #ifdef debug
            cerr << "Create reference from cursor at " << reference_cursor << " out to "
                << target_position << "/" << reference_sequence.size() << endl;
            #endif

            // Don't get out of the chunk
            assert(target_position <= reference_sequence.size());
            assert(reference_cursor <= reference_sequence.size());
            
            if (target_position < reference_cursor) {
                // TODO: should this ever happen? Should we be asked to go backward?
                #ifdef debug
                cerr << "Nothing to do! Already sufficient reference!" << endl;
                #endif
                return;
            }

            // Make new nodes for all the sequence we want to add
            auto new_nodes = create_nodes(reference_sequence.substr(reference_cursor, target_position - reference_cursor));

            // Remember the total node length we have scanned through
            size_t seen_bases = 0;

            if (!new_nodes.empty()) {
                // Add the start node to the starting at map.
                // Interior nodes break at locations that are arbitrary, and we know
                // nothign wants to attach to them there. Plus they're already linked to
                // each other and we shouldn't link them again.
                // Remember where it starts and ends along the reference path
                nodes_starting_at[reference_cursor].insert(new_nodes.front()->id());


                for (Node* node : new_nodes) {
                    // Add matches on the reference path for all the new nodes
                    add_match(ref_path, node);

                    // Remember how long that node was so we place the next one right.
                    seen_bases += node->sequence().size();
                }

                // Add the end node to the ending at map.
                nodes_ending_at[reference_cursor + seen_bases - 1].insert(new_nodes.back()->id());

            }

            // Advance the cursor
            reference_cursor = target_position;
            
            #ifdef debug
            cerr << "Advanced reference cursor to " << reference_cursor << "/" << reference_sequence.size() << endl;
            #endif
            
            assert(reference_cursor <= reference_sequence.size());
            
        };

        while (next_variant != variants.end() || !clump.empty()) {
            // While there are more variants, or while we have the last clump to do...

            // Group variants into clumps of overlapping variants.
            if (clump.empty() || 

                (next_variant != variants.end() && clump_end > next_variant->zeroBasedPosition() - chunk_offset)) {


                // Either there are no variants in the clump, or this variant
                // overlaps the clump. It belongs in the clump
                clump.push_back(&(*next_variant));
                // It may make the clump longer and necessitate adding more variants.
                // TODO: make sure long SVs don't fall outside chunk
                clump_end = max(clump_end, next_variant->zeroBasedPosition() + next_variant->ref.size() - chunk_offset);


                // Try the variant after that
                next_variant++;
            } else {
                // The next variant doesn't belong in this clump.
                // Handle the clump.
                
                #ifdef debug
                cerr << "Handling clump of " << clump.size() << " variants" << endl;
                #endif

                // Parse all the variants into VariantAllele edits

                // This holds a map from Variant pointer to a vector of lists
                // of VariantAllele edits, one list per non-ref allele of the
                // variant.
                map<vcflib::Variant*, vector<list<vcflib::VariantAllele>>> parsed_clump;

                // This determines the order we will process variants in. We use it
                // to sort variants in a clump by hash for the purposes of assigning
                // IDs.
                map<string, vcflib::Variant*> variants_by_name;

                // This holds the min and max values for the starts and ends of
                // edits in each variant that are actual change-making edits. These
                // are in chunk coordinates. They are only populated if a variant
                // has a variable region. They can enclose a 0-length variable
                // region by having the end before the beginning.
                map<vcflib::Variant*, pair<int64_t, int64_t>> variable_bounds;

                // This holds the min and max values for starts and ends of edits
                // not removed from the clump. These are in chunk coordinates.
                int64_t first_edit_start = numeric_limits<int64_t>::max();
                int64_t last_edit_end = -1;

                // We'll fill this with any duplicate variants that should be
                // ignored, out of the clump. This is better than erasing out of a
                // vector.
                set<vcflib::Variant*> duplicates;

                for (vcflib::Variant* variant : clump) {

                    // Check the variant's reference sequence to catch bad VCF/FASTA pairings

                    if (!variant->is_symbolic_sv()){
                        auto expected_ref = reference_sequence.substr(variant->zeroBasedPosition() - chunk_offset, variant->ref.size());

                        if(variant->ref != expected_ref) {
                        // TODO: report error to caller somehow
                        #pragma omp critical (cerr)
                            cerr << "error:[vg::Constructor] Variant/reference sequence mismatch: " << variant->ref
                                << " vs pos: " << variant->position << ": " << expected_ref << "; do your VCF and FASTA coordinates match?"<< endl
                                << "Variant: " << *variant << endl;
                                cerr << "zero ind: " << variant->zeroBasedPosition() << " 1-indexed: " << variant->position << endl;
                            exit(1);
                        }
                    }
                    

                    // Name the variant and place it in the order that we'll
                    // actually construct nodes in (see utility.hpp)
                    string variant_name = make_variant_id(*variant);
                    if (variants_by_name.count(variant_name)) {
                        // Some VCFs may include multiple variants at the same
                        // position with the same ref and alt. We will only take the
                        // first one.
                        #pragma omp critical (cerr)
                        cerr << "warning:[vg::Constructor] Skipping duplicate variant with hash " << variant_name
                            << " at " << variant->sequenceName << ":" << variant->position << endl;
                        duplicates.insert(variant);
                        continue;
                    }

                    variants_by_name[variant_name] = variant;

                    // We need to parse the variant into alts, each of which is a
                    // series of VariantAllele edits. This holds the full alt allele
                    // string and the edits needed to make it. The VariantAlleles
                    // completely cover the alt, and some of them may be perfect
                    // matches to stretches of reference sequence. Note that the
                    // reference allele of the variant won't appear here.

                    map<string, vector<vcflib::VariantAllele>> alternates;
                    if (flat) {
                        alternates = variant->flatAlternates();
                        // if we can, remove the 1bp "standard" base that's added at the beginning of indels
                        for (auto& v : alternates) {
                            for (auto& a : v.second) {
                                if (a.ref[0] == a.alt[0]) {
                                    a.ref = a.ref.substr(1);
                                    a.alt = a.alt.substr(1);
                                    ++a.position;
                                }
                            }
                        }
                    //} else if (!variant->has_sv_tags()) {
                    } else {
                        alternates = variant->parsedAlternates();
                    }
                    if (!variant->is_symbolic_sv()){

                        //map<vcflib::Variant*, vector<list<vcflib::VariantAllele>>> parsed_clump;
                        //auto alternates = use_flat_alts ? variant.flatAlternates() : variant.parsedAlternates();
                        for (auto &kv : alternates){
                     // For each alt in the variant

                        if (kv.first == variant->ref)
                        {
                            // Skip the ref, because we can't make any ref nodes
                            // until all the edits for the clump are known.
                            continue;
                        }


                            // With 0 being the first non-ref allele, which alt are we?
                            // Copy the string out of the map
                            string alt_string = kv.first;
                            // Then look it up
                            size_t alt_index = variant->getAltAlleleIndex(alt_string);

                            if (alt_index >= parsed_clump[variant].size()) {
                                // Make sure we have enough room to store the VariantAlleles for this alt.
                                parsed_clump[variant].resize(alt_index + 1);
                            }

                            // Find the list of edits for this alt
                            auto &alt_parts = parsed_clump[variant][alt_index];

                            #ifdef debug
                            cerr << "Non-ref allele " << alt_index << endl;
                            #endif

                            // Copy all the VariantAlleles into the list
                            alt_parts.assign(kv.second.begin(), kv.second.end());

                            // Condense adjacent perfect match edits, so we only break
                            // matching nodes when necessary.
                            condense_edits(alt_parts);
                        }
                        // Trim the alts down to the variant's (possibly empty) variable
                        // region
                        trim_to_variable(parsed_clump[variant]);
                    } else {
                        cerr << "Is symbolic: " << *variant << endl;
                        // For now, only permit one allele for SVs
                        // in the future, we'll build out VCF lib to fix this.
                        // TODO build out vcflib to fix this.
                        
                        // We need to make sure parsed_clump[variant] has an entry for each allele.
                        // But the contents won't matter since this is an SV.
                        parsed_clump[variant].resize(variant->alt.size());
                    }
                
                    // Get the variable bounds in VCF space for all the trimmed alts of this variant
                    // Note: we still want bounds for SVs, we just have to get them differently
                    std::pair<int64_t, int64_t> bounds;
                    bounds = get_bounds(parsed_clump[variant]);
                    if (variant->is_symbolic_sv()){
                        bounds = get_bounds(*variant);
                    }
                    

                    if (bounds.first != numeric_limits<int64_t>::max() || bounds.second != -1) {
                        // There's a (possibly 0-length) variable region
                        bounds.first -= chunk_offset;
                        bounds.second -= chunk_offset;
                        // Save the bounds for making reference node path visits
                        // inside the ref allele of the variable region.
                        variable_bounds[variant] = bounds;

                        #ifdef debug
                        if (bounds.first < first_edit_start) {
                            cerr << "Expanded first_edit_start to " << bounds.first << " with " << *variant << endl;
                        }
                        if (bounds.second > last_edit_end) {
                            cerr << "Expanded last_edit_end to " << bounds.second << " with " << *variant << endl;
                        }
                        #endif

                        // Expand bounds for the variable region of the chunk as a whole
                        first_edit_start = min(first_edit_start, bounds.first);
                        last_edit_end = max(last_edit_end, bounds.second);
                    }
                }

                // We have to have some non-ref material in the clump, even if it
                // occupies 0 reference space.
                assert(last_edit_end != -1);
                assert(first_edit_start != numeric_limits<int64_t>::max());

                #ifdef debug
                cerr << "edits run between " << first_edit_start << " and " << last_edit_end << endl;
                #endif

                // Create ref nodes from the end of the last clump (where the cursor
                // is) to the start of this clump's interior non-ref content.
                add_reference_nodes_until(first_edit_start);

                // This keeps track of edits that already have nodes, consisting of
                // a ref position, a ref sequence, and an alt sequence. It maps to a
                // vector of pointers to the nodes created, which are owned by the
                // graph. The vector is to handle the case where an edit is too long
                // to be all one node, according to our max node length, and is
                // always nonempty.
                map<tuple<long, string, string>, vector<Node*>> created_nodes;

                // This holds on to variant ref paths, which we can't actually fill
                // in until all the variants in the clump have had their non-ref
                // paths done.
                map<vcflib::Variant*, Path*> variant_ref_paths;

                for (auto& kv : variants_by_name) {
                    // For each variant in the clump, sorted by name
                    auto& variant_name = kv.first;
                    auto* variant = kv.second;
                    
                    #ifdef debug
                    cerr << "Process variant " << variant_name << " with " << parsed_clump[variant].size() << " alts" << endl;
                    #endif

                    if (alt_paths) {
                        // Declare its ref path straight away.
                        // We fill in the ref paths after we make all the nodes for the edits.
                        variant_ref_paths[variant] = to_return.graph.add_path();
                        variant_ref_paths[variant]->set_name("_alt_" + variant_name + "_0");
                    }

                    for (size_t alt_index = 0; alt_index < parsed_clump[variant].size(); alt_index++) {                
                        // For each non-ref alt in the parsed variant

                        // Name the alt after the number that this allele has.
                        // We have to bump the allele index because the first alt is 0.
                        string alt_name = "_alt_" + variant_name + "_" + to_string(alt_index + 1);

                        // There should be a path named after it.
                        Path* alt_path = nullptr;
                        if (alt_paths) {
                            alt_path = to_return.graph.add_path();
                            alt_path->set_name(alt_name);
                        }

                    // SV HAX
                    if (this->do_svs && variant->has_sv_tags() && variant->canonical){

                            auto e_start = variant->zeroBasedPosition() - chunk_offset;
                            // TODO check index here, may or may not need to subtract 1
                            auto e_end = variant->zeroBasedPosition() + abs(std::stol(variant->info.at("SVLEN")[0])) - chunk_offset - 1;

                            // Make in between nodes by grabbing our sequence from the fasta(s),
                            // either from the reference (deletions) or from insertion sequences.
                            auto key = make_tuple(variant->zeroBasedPosition() - chunk_offset, variant->info.at("SVTYPE")[0], "");

                            string sv_type = variant->info.at("SVTYPE")[0];

                            if (variant->info.at("SVTYPE")[0] == "INS"){

                                // Create insertion sequence nodes
                                if (created_nodes.count(key) == 0){
                                    vector<Node*> node_run = create_nodes(variant->info.at("SEQ")[0]);


                                    nodes_starting_at[e_start].insert(node_run.front()->id());
                                    nodes_ending_at[e_end].insert(node_run.back()->id());

                                    inserts.insert(node_run.front()->id());
                                    inserts.insert(node_run.back()->id());

                                    created_nodes[key] = node_run;

                                    if (alt_paths) {
                                        for (Node* node : created_nodes[key]) {
                                            // Add a visit to each node we created/found in
                                            // order to the path for this alt of this
                                            // variant.
                                            add_match(alt_path, node);
                                        }
                                    }
                                }
                        }

                        else if (sv_type == "DEL"){
                            if (created_nodes.count(key) == 0){

                                size_t arc_end = variant->zeroBasedPosition() - chunk_offset + std::stol(variant->info.at("SVLEN")[0]);
                                int64_t arc_start = (int64_t) variant->zeroBasedPosition() - chunk_offset; 

                                deletions_ending_at[arc_end].insert(arc_start);
                                deletion_starts.insert(arc_start);


                                if (alt_paths) {
                                    deletion_start_to_alt_name[arc_start] = alt_name;
                                }
                            }
                        }
                        else if (sv_type == "INV"){
                            // Handle inversions
                            // We only need reference nodes, plus two arcs
                            // one from the inverted sequence's beginning to the sequence following
                            // its last node and
                            // one from the end of the sequence preceding the inversion to the back 
                            // of the inverted sequence's last node.
                                    
                            size_t inv_end = variant->zeroBasedPosition() - chunk_offset + std::stol(variant->info.at("SVLEN")[0]);
                            int64_t inv_start = (int64_t) variant->zeroBasedPosition() - chunk_offset;
                            // inversion_starts[inv_start - 1].insert(inv_end);
                            // inversion_ends[inv_end + 1].insert(inv_start);

                            inversion_starts[inv_start].insert(inv_end);
                            inversion_ends[inv_end].insert(inv_start);
                        }
                        else {
                            // Unknown or unsupported SV type
                                cerr << "warning:[vg::Constructor]: unrecognized SV type " << sv_type << endl;
                            }
                        } else {
                            // This is not an SV
                            #ifdef debug
                            cerr << "Process alt " << (alt_index + 1) << " of variant " << variant_name << " as an ordinary variant" << endl;
                            #endif
                        
                            for (vcflib::VariantAllele& edit : parsed_clump[variant][alt_index]) {
                                // For each VariantAllele used by the alt
                                #ifdef debug
                                cerr << "Apply " << edit.ref << " -> " << edit.alt << " @ " << edit.position << endl;
                                #endif

                                if (edit.alt != "") {
                                    // This is a visit to a node for the alt
                                    // We need a key to see if a node has been made for this edit already
                                    auto key = make_tuple(edit.position - 1 - chunk_offset, edit.ref, edit.alt);

                                    if (created_nodes.count(key) == 0) {
                                        // We don't have a run of nodes for this edit, so make one.
                                        vector<Node*> node_run = create_nodes(edit.alt);

                                        // Compute where the edit starts and ends in local chunk coordinates
                                        auto edit_start = edit.position - 1 - chunk_offset;
                                        auto edit_end = edit.position - 1 - chunk_offset + edit.ref.size() - 1;


                                        #ifdef debug
                                        cerr << "Created nodes running " << edit_start << " to " << edit_end << endl;
                                        #endif

                                        // Remember where the first one starts and the last one ends, for wiring up later.
                                        nodes_starting_at[edit_start].insert(node_run.front()->id());
                                        nodes_ending_at[edit_end].insert(node_run.back()->id());

                                        // Save it in case any other alts also have this edit.
                                        created_nodes[key] = node_run;

                                        if (edit.ref == "") {
                                            // This is an insert, so mark its ends as
                                            // such, so they don't connect to other
                                            // insert ends.
                                            inserts.insert(node_run.front()->id());
                                            inserts.insert(node_run.back()->id());

                                            #ifdef debug
                                            cerr << "Nodes are insert" << endl;
                                            #endif
                                        }
                                    } else {
                                        #ifdef debug
                                        cerr << "Found existing nodes" << endl;
                                        #endif
                                    }

                                    if (alt_paths) {
                                        for (Node* node : created_nodes[key]) {
                                            // Add a visit to each node we created/found in
                                            // order to the path for this alt of this
                                            // variant.
                                            add_match(alt_path, node);
                                        }
                                    }

                                } else if (edit.ref != "") {
                                    // It's a deletion (and not a weird ""->"" edit).

                                    // Add an entry to the deletion arcs
                                    // What is the past-the-end position (first non-deleted)
                                    size_t arc_end = edit.position - 1 - chunk_offset + edit.ref.size();
                                    // What is the before-the-beginning position (last non-deleted, may be -1)
                                    int64_t arc_start = (int64_t) edit.position - 1 - chunk_offset - 1;


                                    #ifdef debug
                                    cerr << "Ensure deletion arc " << arc_start << " to " << arc_end << endl;
                                    #endif

                                    // Add the arc (if it doesn't exist). We only index
                                    // arcs from the end, because we'll make them when
                                    // looking for predecessors of nodes. TODO: could we
                                    // make special handling of deletions that go to -1
                                    // more efficient?
                                    deletions_ending_at[arc_end].insert(arc_start);

                                    // Remember that an arc comes from this base
                                    deletion_starts.insert(arc_start);
                                }

                            }
                        }

                    }
                }

                // Then after you finish all the alts, add the ref nodes that
                // haven't already been created, breaking wherever something can
                // come in or out.

                // We need a function to work that out
                auto next_breakpoint_after = [&](size_t position) -> size_t {
                    // This returns the position of the base to the left of the next
                    // required breakpoint within this clump, after the given
                    // position, given created nodes and deletions that already
                    // exist.

                    // If nothing else, we're going to break at the end of the last
                    // edit in the clump.
                    size_t to_return = last_edit_end;
                    
                    #ifdef debug
                    cerr << "Next breakpoint must be at or before " << last_edit_end << endl;
                    #endif

                    // See if any nodes are registered as starting after our
                    // position. They'll all start before the end of the clump, and
                    // we don't care if they start at our position since that
                    // breakpoint already happened.
                    auto next_start_iter = nodes_starting_at.upper_bound(position);

                    if(next_start_iter != nodes_starting_at.end()) {
                        // If we found something, walk back where the breakpoint
                        // needs to be so we break before that node starts.
                        to_return = min(to_return, next_start_iter->first - 1);
                        #ifdef debug
                        cerr << "Next node starts at " << next_start_iter->first - 1 << endl;
                        #endif
                    }

                    // See if any nodes are registered as ending at or after our
                    // position. We do care if they end at our position, since that
                    // means we need to break right here.
                    auto next_end_iter = nodes_ending_at.lower_bound(position);

                    if(next_end_iter != nodes_ending_at.end()) {
                        // If we found something, we need to break where that node
                        // ends.
                        to_return = min(to_return, next_end_iter->first );
                        #ifdef debug
                        cerr << "Next node ends at " << next_end_iter->first << endl;
                        #endif
                    }

                    // See if any deletions are registered as ending after here.
                    // Deletions break the reference before their past-the-end base,
                    // so we don't care about deletions ending here exactly.
                    auto deletion_end_iter = deletions_ending_at.upper_bound(position);

                    if(deletion_end_iter != deletions_ending_at.end()) {
                        // If we found something, walk back where the breakpoint
                        // needs to be so we break before the node after the
                        // deletion starts.
                        to_return = min(to_return, deletion_end_iter->first - 1);
                        #ifdef debug
                        cerr << "Next deletion ends at " << deletion_end_iter->first - 1 << endl;
                        #endif
                    }

                    // See if any deletions are known to start at or after this
                    // base. We care about exact hits now, because deletions break
                    // after the base they start at.
                    auto deletion_start_iter = deletion_starts.lower_bound(position);
                    // We don't need to worry about -1s here. They won't be found
                    // with lower_bound on a size_t.

                    if(deletion_start_iter != deletion_starts.end()) {
                        // If we found something, walk back where the breakpoint
                        // needs to be so we break at the position the deletion
                        // needs to leave from.
                        to_return = min(to_return, (size_t)*deletion_start_iter);
                        #ifdef debug
                        cerr << "Next deletion starts at " << *deletion_start_iter << endl;
                        #endif
                    }

                    // Check to see if any inversions happen past this point
                    // Inversions break the reference twice, much like deletions.
                    auto inv_end_iter = inversion_ends.upper_bound(position);
                    if (inv_end_iter != inversion_ends.end()){
                        to_return = min(to_return, (size_t) inv_end_iter->first - 1);
                        #ifdef debug
                        cerr << "Next inversion ends at " << inv_end_iter->first - 1 << endl;
                        #endif
                    }

                    auto inv_start_iter = inversion_starts.lower_bound(position);
                    if (inv_start_iter != inversion_starts.end()){
                        to_return = min(to_return, (size_t) inv_start_iter->first);
                        #ifdef debug
                        cerr << "Next inversion starts at " << inv_start_iter->first << endl;
                        #endif
                    }
                    
                    #ifdef debug
                    cerr << "Selected " << to_return << " as breakpoint" << endl;
                    #endif

                    return to_return;

                };

                // Note that in some cases (i.e. pure inserts) we may not need any
                // ref nodes at all. Also note that in other cases (variants with
                // exterior matches) some ref nodes overlapping the variant may not
                // really belong on the ref path for the variant, because the alt
                // path for the variant starts/end further in.

                while (reference_cursor < last_edit_end + 1) {
                    // Until we hit the end

                    // Find where the next node run must end to attach to stuff
                    size_t next_end = next_breakpoint_after(reference_cursor);
                    
                    #ifdef debug
                    cerr << "Creating reference nodes from " << reference_cursor << " out to "
                        << next_end << "/" << reference_sequence.size() << endl;
                    #endif
                    
                    assert(reference_cursor <= reference_sequence.size());
                    assert(next_end <= reference_sequence.size());

                    // We need to have a reference node/run of nodes (which may have
                    // already been created by a reference match) between where the
                    // cursor is and where the next breakpoint has to be.
                    // This is the sequence it should have.
                    string run_sequence = reference_sequence.substr(reference_cursor, next_end - reference_cursor + 1);

                    // We need a key to see if a node (run) has been made for this sequece already
                    auto key = make_tuple(reference_cursor, run_sequence, run_sequence);

                    if (created_nodes.count(key) == 0) {
                        // We don't have a run of ref nodes up to the next break, so make one
                        vector<Node*> node_run = create_nodes(run_sequence);

                        // Remember where the first one starts and the last one ends, for wiring up later.
                        nodes_starting_at[reference_cursor].insert(node_run.front()->id());
                        nodes_ending_at[next_end].insert(node_run.back()->id());

#ifdef debug
                        cerr << "Created reference nodes running " << reference_cursor << " to " << next_end << endl;
#endif

                        // Save it in case any other alts also have this edit.
                        created_nodes[key] = node_run;
                    }

                    for (Node* node : created_nodes[key]) {
                        // Add a reference visit to each node we created/found
                        add_match(ref_path, node);

                        if (alt_paths) {
                            for (vcflib::Variant* variant : clump) {
                                // For each variant we might also be part of the ref allele of
                                if (!duplicates.count(variant) &&
                                        variable_bounds.count(variant) &&
                                        reference_cursor >= variable_bounds[variant].first &&
                                        reference_cursor <= variable_bounds[variant].second) {
                                    // For unique variants that actually differ from reference,
                                    // if this run of nodes starts within the variant's variable region...
                                    // (We know if it starts in the variable region it has to
                                    // end in the variant, because the variable region ends with
                                    // a node break)

                                    if (variant_ref_paths.count(variant) == 0) {
                                        // All unique variants ought to have a ref path created
                                        cerr << "error:[vg::Constructor] no ref path for " << *variant << endl;
                                        exit(1);
                                    }

                                    // Add a match along the variant's ref allele path
                                    add_match(variant_ref_paths[variant], node);
                                }
                            }
                        }
                    }

                    // Advance the reference cursor to after this run of reference nodes
                    reference_cursor = next_end + 1;
                    
                    assert(reference_cursor <= reference_sequence.size());

                    // Keep going until we have created reference nodes through to
                    // the end of the clump's interior edits.
                }

                // Now we have gotten through all the places where nodes start, before the end of the clump.

                // Now the clump is handled
                clump.clear();
                clump_end = 0;
                // On the next loop we'll grab the next variant for the next clump.
            }
        }

        // Create reference path nodes and mappings after the last clump.
        add_reference_nodes_until(reference_sequence.size());


        // Create all the edges
        for (auto& kv : nodes_starting_at) {
            if (kv.first == 0) {
                // These are the nodes that abut the left edge of the chunk. Add
                // each of these nodes to the set of left-edge-abuting nodes.
                for (auto& node_id : kv.second) {
                    to_return.left_ends.insert(node_id);
                }
            } else {
                // These are nodes that start somewhere else.
                for (auto& right_node : kv.second) {
                    // For every node that could occur here

                    for (auto& left_node : nodes_ending_at[kv.first - 1]) {
                        // For every node that could come before these nodes

                        if (inserts.count(left_node) && inserts.count(right_node)) {
                            // Don't connect two inserts at the same position (or an insert to itself).
#ifdef debug
                            cerr << "Skip insert-insert edge " << left_node << " -> " << right_node << endl;
#endif
                            continue;
                        }

#ifdef debug
                        cerr << "Add normal edge " << left_node << " -> " << right_node << endl;
#endif

                        // Emit an edge
                        auto* edge = to_return.graph.add_edge();
                        edge->set_from(left_node);
                        edge->set_to(right_node);
                    }

                    

                    // Now we do the deletions. We want to allow daisy-chaining
                    // deletions.

                    // We compose a set of deletion start points.
                    set<int64_t> possible_starts;

                    // We also keep a list of unexplored deletion end points to chain from.
                    list<int64_t> possible_ends;
                    possible_ends.push_back(kv.first);

                    // And a set of explored ones
                    set<int64_t> explored_ends;

                    while (!possible_ends.empty()) {
                        // Find an unexplored place where we can find more daisy-
                        // chained deletions ending.
                        int64_t deletion_end = possible_ends.front();
                        possible_ends.pop_front();

                        for (auto& deletion_start : deletions_ending_at[deletion_end]) {
                            // For every deletion start that can end there.

                            // Note that we can delete from there to our node along
                            // transitive deletions.
                            possible_starts.insert(deletion_start);

                            // We can daisy chain from deletions that end at the
                            // base after this deletion starts.
                            int64_t possible_end = deletion_start + 1;

                            if(chain_deletions && possible_end > 0 && !explored_ends.count(possible_end)) {
                                // Queue it up if not already queued. If we aren't
                                // chaining deletions, we'll only look at the starts
                                // accessible from the root of our search.
                                possible_ends.push_back(possible_end);
                                explored_ends.insert(possible_end);
                            }
                        }
                    }

                    for (auto& deletion_start : possible_starts) {
                        // For everywhere a deletion can start that comes to here

                        if (deletion_start == -1) {
                            // This node ought to be exposed on the left actually.
                            to_return.left_ends.insert(right_node);

                        } else {
                            // The deletion doesn't go all the way to the left edge
                            // but actually starts at a place where there are nodes.

                            for (auto& left_node : nodes_ending_at[deletion_start]) {
                                // For every node that the deletion could start with

                                if (inserts.count(left_node)) {
                                    // Don't let an inserted node happen just before a deletion.
#ifdef debug
                                    cerr << "Skip insertion-deletion edge " << left_node << " -> " << right_node << endl;
#endif
                                    continue;
                                }

#ifdef debug
                                cerr << "Add deletion edge " << left_node << " -> " << right_node << endl;
#endif

                                // Emit an edge
                                auto* edge = to_return.graph.add_edge();
                                edge->set_from(left_node);
                                edge->set_to(right_node);

                            }
                        }
                    }

                    for (auto& inv_end : inversion_starts[kv.first]){
                        for (auto& n : nodes_starting_at[inv_end]){
                            auto* e = to_return.graph.add_edge();
                            e->set_from(right_node);
                            e->set_to(n);
                            e->set_from_start(true);
                            e->set_to_end(false);
                        }
                    }
                    for (auto& inv_start : inversion_ends[kv.first]){
                        for (auto& n : nodes_ending_at[inv_start]){
                            auto* e = to_return.graph.add_edge();
                            e->set_from(n);
                            e->set_to(right_node);
                            e->set_to_end(true);
                            e->set_from_start(false);
                        }
                    }
                    


                }
            }
        }

    

        for(auto& node_id : nodes_ending_at[reference_sequence.size() - 1]) {
            // Add each node that ends at the end of the chunk to the set of such nodes
            to_return.right_ends.insert(node_id);
        }

        for(auto& deletion_start : deletions_ending_at[reference_sequence.size()]) {
            // Also add in nodes at the starts of deletions that go to the end of the chunk

            if(deletion_start == -1) {
                // Note that we don't handle completely spanning deletions. But
                // those can't be articulated in VCF anyway because alts can't be
                // empty.
                continue;
            }

            for (auto& node_id : nodes_ending_at[deletion_start]) {
                // For every node that the deletion could start with
                // Expose it on the right of the graph
                to_return.right_ends.insert(node_id);
            }
        }

        // Remember to tell the caller how many IDs we used
        to_return.max_id = next_id - 1;

        return to_return;
    }

    void Constructor::construct_graph(string vcf_contig, FastaReference& reference, VcfBuffer& variant_source,
        const vector<FastaReference*>& insertions, function<void(Graph&)> callback) {

        // Our caller will set up indexing. We just work with the buffered source that we pull variants from.

        // What sequence are we looking for in the fasta? The one we were passed, unless it was renamed.
        string reference_contig = vcf_to_fasta(vcf_contig);

        // At what offset in the reference sequence do we start?
        size_t leading_offset;
        // At what position in the reference sequence do we stop (past-the-end)?
        size_t reference_end;
        
        if (allowed_vcf_regions.count(vcf_contig)) {
            // Only look at the region we were asked for. We will only find variants
            // *completely* contained in this region! Partially-overlapping variants
            // will be discarded!
            leading_offset = allowed_vcf_regions[vcf_contig].first;
            reference_end = allowed_vcf_regions[vcf_contig].second;
        } else {
            // Look at the whole contig
            leading_offset = 0;
            reference_end = reference.sequenceLength(reference_contig);
        }
        
#ifdef debug
        cerr << "building contig for chunk of reference " << reference_contig << " in interval " << leading_offset << " to " << reference_end << endl;
#endif

        // Set up a progress bar through the chromosome
        create_progress("building graph for " + vcf_contig, reference_end - leading_offset);

        // Scan through variants until we find one that is on this contig and in this region.
        // If we're using an index, we ought to already be at the right place.
        variant_source.fill_buffer();
        while(variant_source.get() && (variant_source.get()->sequenceName != vcf_contig ||
                    variant_source.get()->zeroBasedPosition() < leading_offset ||
                    variant_source.get()->zeroBasedPosition() + variant_source.get()->ref.size() > reference_end)) {
            // This variant comes before our region

            // Discard variants that come out that are before our region
            variant_source.handle_buffer();
            variant_source.fill_buffer();
        }

        // Now we're on the variants we actually want.

        // This is where the next chunk will start in the reference sequence.
        size_t chunk_start = leading_offset;

        // We maintain a growing list of variants that will go into a chunk. They
        // are all positioned relative to chunk_start.
        vector<vcflib::Variant> chunk_variants;
        // And we track the largest past-the-end position of all the variants
        size_t chunk_end = 0;

        // For chunk wiring, we need to remember the nodes exposed on the end of the
        // previous chunk.
        set<id_t> exposed_nodes;

        // And we need to do the same for ranks on the reference path? What's the
        // max rank used?
        size_t max_ref_rank = 0;

        // Whenever a chunk ends with a single node, we separate it out and buffer
        // it here, because we may need to glue it together with subsequent leading
        // nodes that were broken by a chunk boundary.
        Node last_node_buffer;

        // Sometimes we need to emit single node reference chunks gluing things
        // together
        auto emit_reference_node = [&](Node& node) {

            // Don't emit nonexistent nodes
            assert(node.id() != 0);

            // Make a single node chunk for the node
            Graph chunk;
            *(chunk.add_node()) = node;

            // It needs a primary path mapping.
            Path* path = chunk.add_path();
            path->set_name(reference_contig);
            Mapping* mapping = path->add_mapping();
            mapping->mutable_position()->set_node_id(node.id());
            // With a rank
            mapping->set_rank(++max_ref_rank);
            // And an edit
            Edit* edit = mapping->add_edit();
            edit->set_from_length(node.sequence().size());
            edit->set_to_length(node.sequence().size());

            // Emit this chunk we were holding back.
            callback(chunk);
        };

        // When a chunk gets constructed, we'll call this handler, which will wire
        // it up to the previous chunk, if any, and then call the callback we're
        // supposed to send our graphs out through.
        // Modifies the chunk in place.
        auto wire_and_emit = [&](ConstructedChunk& chunk) {
            // When each chunk comes back:

            if (chunk.left_ends.size() == 1 && last_node_buffer.id() != 0) {
                // We have a last node from the last chunk that we want to glom onto
                // this chunk.

                // We want to merge it with the single source node for this
                // chunk. But depending on the variant structure it may not be
                // the first node generated (because we generate variant alt
                // material first, and a variant may lead the chunk). So we do
                // a linear scan.
                
                // This seems slow, but actually shouldn't be: most of the time
                // we succeed on the first try, the whole process is linear in
                // graph size anyway, and we never have to scan through more
                // than a variant's worth of nodes.
                
                // This is the node we want
                auto wanted_id = *chunk.left_ends.begin();
                
                // We will fill this in
                Node* mutable_first_node = nullptr;
                for (size_t i = 0; i < chunk.graph.node_size(); i++) {
                    // Look at each node in turn
                    mutable_first_node = chunk.graph.mutable_node(i);
                    
                    if (mutable_first_node->id() == wanted_id) {
                        // We found the left end we want
                        break;
                    }
                }
                
                // Make sure we found it
                assert(mutable_first_node != nullptr && mutable_first_node->id() == wanted_id);

                // Combine the sequences for the two nodes
                string combined_sequence = last_node_buffer.sequence() + mutable_first_node->sequence();

                if (combined_sequence.size() <= max_node_size) {
                    // We can fit both nodes into one node.
                    mutable_first_node->set_sequence(combined_sequence);

                    // We can re-use the ID from the last node, which we discard.
                    // Edges to it will get rerouted to the first node. And we know
                    // it can't have any mappings except the primary path.
                    max_id--;

                    // We don't need any edges to it, either.
                    exposed_nodes.clear();

                    // Clear the buffer since we moved its sequence and ID into the
                    // graph.
                    last_node_buffer = Node();
                } else {
                    // We need to keep two nodes. Reapportion the sequence between
                    // them according to our division algorithm. TODO: can sometimes
                    // differ from old construct behavior, but this way will be
                    // better.
                    size_t piece_size = greedy_pieces ? max_node_size : ((combined_sequence.size() + 1) / 2);
                    last_node_buffer.set_sequence(combined_sequence.substr(0, piece_size));
                    mutable_first_node->set_sequence(combined_sequence.substr(piece_size));

                    // Emit the buffered node as a chunk
                    emit_reference_node(last_node_buffer);
                    // Clear it
                    last_node_buffer = Node();
                }

                // Update the mapping lengths on the mutable first node.
                // First we find the primary path
                Path* path = chunk.graph.mutable_path(0);
                assert(path->name() == reference_contig);
                // Then the first mapping
                Mapping* mapping = path->mutable_mapping(0);
                assert(mapping->position().node_id() == mutable_first_node->id());
                assert(mapping->edit_size() == 1);
                // Then the only edit
                Edit* edit = mapping->mutable_edit(0);
                // Correct its length
                edit->set_from_length(mutable_first_node->sequence().size());
                edit->set_to_length(mutable_first_node->sequence().size());
            } else if (last_node_buffer.id() != 0) {
                // There's no single leading node on this next chunk, but we still
                // have a single trailing node to emit.

                // Emit it
                emit_reference_node(last_node_buffer);
                // Clear it
                last_node_buffer = Node();
            }

            if (chunk.right_ends.size() == 1) {
                // We need to pull out the last node in the chunk. Note that it may
                // also be the first node in the chunk...

                // We know it's the last node in the graph
                last_node_buffer = chunk.graph.node(chunk.graph.node_size() - 1);


                assert(chunk.right_ends.count(last_node_buffer.id()));

                // Remove it
                chunk.graph.mutable_node()->RemoveLast();

                // Find the primary path
                Path* path = chunk.graph.mutable_path(0);
                assert(path->name() == reference_contig);
                // Then drop last mapping, which has to be to this node
                assert(path->mapping_size() > 0);
                assert(path->mapping(path->mapping_size() - 1).position().node_id() == last_node_buffer.id());
                path->mutable_mapping()->RemoveLast();

                // Update its ID separately, since it's no longer in the graph.
                last_node_buffer.set_id(last_node_buffer.id() + max_id);
            }

            // Up all the IDs in the graph
            // TODO: this is repeating code that vg::VG has...
            for (size_t i = 0; i < chunk.graph.node_size(); i++) {
                // For each node
                auto* node = chunk.graph.mutable_node(i);
                // Bump the node ID
                node->set_id(node->id() + max_id);
            }
            for (size_t i = 0; i < chunk.graph.edge_size(); i++) {
                // For each edge
                auto* edge = chunk.graph.mutable_edge(i);
                // Bump the edge end IDs
                edge->set_from(edge->from() + max_id);
                edge->set_to(edge->to() + max_id);
            }
            for (size_t i = 0; i < chunk.graph.path_size(); i++) {
                // For each path
                auto* path = chunk.graph.mutable_path(i);
                for (size_t j = 0; j < path->mapping_size(); j++) {
                    // For each mapping in the path
                    auto* mapping = path->mutable_mapping(j);

                    // Bump the ID for the mapping's position
                    mapping->mutable_position()->set_node_id(mapping->position().node_id() + max_id);

                    // Set the rank.
                    // TODO: we're just clobbering the ref path ranks that were generated in chunk construction.
                    mapping->set_rank(++max_ref_rank);
                }
            }

            // If there was a previous ConstructedChunk, wire up the edges between them
            for (auto& from_id : exposed_nodes) {
                // For every dangling end in the last chunk

                for (auto& to_id : chunk.left_ends) {
                    // For every node in the new chunk we can wire it to

                    // Make the edge in the new chunk
                    Edge* new_edge = chunk.graph.add_edge();
                    new_edge->set_from(from_id);
                    // Make sure to correct the number in the to set.
                    new_edge->set_to(to_id + max_id);
                }
            }

            // Save the right-side ends from this chunk for the next one, if any
            exposed_nodes.clear();
            for (auto& from_id : chunk.right_ends) {
                // Make sure to correct each ID
                exposed_nodes.insert(from_id + max_id);
            }

            // Remember the new max id, accounting for all the IDs used by this
            // chunk.
            max_id += chunk.max_id;

            // Emit the chunk's graph via the callback
            callback(chunk.graph);
        };

        bool do_external_insertions = false;
        FastaReference* insertion_fasta;


        if (insertions.size() == 1){
            // If we only get one fasta file for insertions, we'll
            // open it and take all insert sequences from there.
            do_external_insertions = true;
            insertion_fasta = insertions[0];
        }
        else if (insertions.size() > 1){
            // if we have more than one insertion fasta file, we can pull
            // sequences from the vcf:fasta pair (i.e. the same index in the vectors).
            do_external_insertions = true;
            cerr << "Passing multiple insertion files not implemented yet." << endl                                                                                    << "Please try combining all of your insertions fastas into one file." << endl;
            exit(1);
        }   
        else{
            // We didn't get any insertion fastas, so we will only handle
            // those with seqs in the vcf.

        }

        while (variant_source.get() && variant_source.get()->sequenceName == vcf_contig &&
                variant_source.get()->zeroBasedPosition() >= leading_offset &&
                variant_source.get()->zeroBasedPosition() + variant_source.get()->ref.size() <= reference_end) {

            // While we have variants we want to include
            auto vvar = variant_source.get();


            // We need to decide if we want to use this variant. By default we will use all variants.
            bool variant_acceptable = true;
            
            if (vvar->is_symbolic_sv() && this->do_svs) {
                // Canonicalize the variant and see if that disqualifies it.
                // This also takes care of setting the variant's insertion sequences.
                variant_acceptable = vvar->canonicalize(reference, insertions, true);
 
                if (variant_acceptable) {
                    // Worth checking for multiple alts.
                    if (vvar->alt.size() > 1) {
                        // We can't handle multiallelic SVs yet.
                        #pragma omp critical (cerr)
                        cerr << "warning:[vg::Constructor] Unsupported multiallelic SV being skipped: " << *vvar << endl;
                        variant_acceptable = false;
                    }
                }
                    
                if (variant_acceptable) {
                    // Worth checking for bounds problems.
                    // We have seen VCFs where the variant positions are on GRCh38 but the END INFO tags are on GRCh37.
                    auto bounds = get_bounds(*vvar);
                    if (bounds.second < bounds.first) {
                        #pragma omp critical (cerr)
                        cerr << "warning:[vg::Constructor] SV with end position before start being skipped (check liftover?): "
                            << *vvar << endl;
                        variant_acceptable = false;
                    }
                }
            }
            
            for (string& alt : vvar->alt) {
                // Validate each alt of the variant

                if(!allATGCN(alt)) {
                    // It may be a symbolic allele or something. Skip this variant.
                    variant_acceptable = false;
                    if (this->do_svs && vvar->is_symbolic_sv() && vvar->canonicalizable()){
                        // Only try to normalize SVs if we want to handle SVs,
                        // the variant is symbolic (i.e. no ref/alts) and the variant
                        // can be canonicalized (it has at least a type and a length)
                        variant_acceptable = vvar->canonicalize(reference, insertions, true);
                    }
                    else{
                        #pragma omp critical (cerr)
                        {
                            bool warn = true;
                            if (!alt.empty() && alt[0] == '<' && alt[alt.size()-1] == '>') {
                                if (symbolic_allele_warnings.find(alt) != symbolic_allele_warnings.end()) {
                                    warn = false;
                                } else {
                                    symbolic_allele_warnings.insert(alt);
                                }
                            }
                            if (warn) {
                                cerr << "warning:[vg::Constructor] Unsupported variant allele \"" << alt << "\"; Skipping variant(s) " << *vvar <<" !" << endl;
                            }
                        }
                        break;
                    }
                    
                }
            }


            if (!variant_acceptable) {
                // Skip variants that have symbolic alleles or other nonsense we can't parse.
                variant_source.handle_buffer();
                variant_source.fill_buffer();
            } else if (!chunk_variants.empty() && chunk_end > vvar->zeroBasedPosition()) {
                // If the chunk is nonempty and this variant overlaps what's in there, put it in too and try the next.
                // TODO: this is a lot like the clumping code...

                // Add it in
                chunk_variants.push_back(*(vvar));
                // Expand out how big the chunk needs to be, so we can get other overlapping variants.
                chunk_end = max(chunk_end, chunk_variants.back().zeroBasedPosition() + chunk_variants.back().ref.size());

                // Try the next variant
                variant_source.handle_buffer();
                variant_source.fill_buffer();

            } else if(chunk_variants.size() < vars_per_chunk && variant_source.get()->zeroBasedPosition() < chunk_start + bases_per_chunk) {
                // Otherwise if this variant is close enough and the chunk isn't too big yet, put it in and try the next.

                // TODO: unify with above code?

                // Add it in
                chunk_variants.push_back(*(vvar));
                // Expand out how big the chunk needs to be, so we can get other overlapping variants.
                chunk_end = max(chunk_end, chunk_variants.back().zeroBasedPosition() + chunk_variants.back().ref.size());

                // Try the next variant
                variant_source.handle_buffer();
                variant_source.fill_buffer();

            } else {
                // This variant shouldn't go in this chunk.

                // Finish the chunk to a point before the next variant, before the
                // end of the reference, before the max chunk size, and after the
                // last variant the chunk contains.
                chunk_end = max(chunk_end,
                        min((size_t ) vvar->zeroBasedPosition(),
                            min((size_t) reference_end,
                                (size_t) (chunk_start + bases_per_chunk))));

                // Get the ref sequence we need
                auto chunk_ref = reference.getSubSequence(reference_contig, chunk_start, chunk_end - chunk_start);

                // Call the construction
                auto result = construct_chunk(chunk_ref, reference_contig, chunk_variants, chunk_start);

                // Wire up and emit the chunk graph
                wire_and_emit(result);

                // Say we've completed the chunk
                update_progress(chunk_end - leading_offset);

                // Set up a new chunk
                chunk_start = chunk_end;
                chunk_end = 0;
                chunk_variants.clear();

                // Loop again on the same variant.
            }
        }

        // We ran out of variants, so finish this chunk and all the others after it
        // without looking for variants.
        // TODO: unify with above loop?
        while (chunk_start < reference_end) {
            // We haven't finished the whole reference

            // Make the chunk as long as it can be
            chunk_end = max(chunk_end,
                    min((size_t) reference_end,
                        (size_t) (chunk_start + bases_per_chunk)));

            // Get the ref sequence we need
            auto chunk_ref = reference.getSubSequence(reference_contig, chunk_start, chunk_end - chunk_start);

            // Call the construction
            auto result = construct_chunk(chunk_ref, reference_contig, chunk_variants, chunk_start);

            // Wire up and emit the chunk graph
            wire_and_emit(result);

            // Say we've completed the chunk
            update_progress(chunk_end - leading_offset);

            // Set up a new chunk
            chunk_start = chunk_end;
            chunk_end = 0;
            chunk_variants.clear();
        }

        // All the chunks have been wired and emitted.
        
        if (last_node_buffer.id() != 0) {
            // Now emit the very last node, if any
            emit_reference_node(last_node_buffer);
            // Update the max ID with that last node, so the next call starts at the next ID
            max_id = max(max_id, (id_t) last_node_buffer.id());
        }

        destroy_progress();

    }

    void Constructor::construct_graph(const vector<FastaReference*>& references,
        const vector<vcflib::VariantCallFile*>& variant_files, const vector<FastaReference*>& insertions,
        function<void(Graph&)> callback) {

        // Make a map from contig name to fasta reference containing it.
        map<string, FastaReference*> reference_for;
        for (size_t i = 0; i < references.size(); i++) {
            // For every FASTA reference, make sure it has an index
            auto* reference = references[i];
            assert(reference->index);
            for (auto& kv : *(reference->index)) {
                // For every sequence name and index entry, point to this reference
                reference_for[kv.first] = reference;
#ifdef debug
                cerr << "Contig " << kv.first << " is in reference " << i << endl;
#endif
            }
        }

        // Make VcfBuffers on all the variant files.
        vector<unique_ptr<VcfBuffer>> buffers;
        for (auto* vcf : variant_files) {
            // Every VCF gets a buffer wrapped around it.

            if (!vcf->is_open()) {
                // Except those that didn't open.
                continue;
            }

            // These will all get destructed when the vector goes away.
            buffers.emplace_back(new VcfBuffer(vcf));
        }

        if (!allowed_vcf_names.empty()) {
            // If we have a set of contigs to do, do those directly.

            for (string vcf_name : allowed_vcf_names) {
                // For each VCF contig, get the FASTA name
                string fasta_name = vcf_to_fasta(vcf_name);

#ifdef debug
                cerr << "Make graph for " << vcf_name << " = " << fasta_name << endl;
#endif

                // Also the FASTA reference that has that sequence
                assert(reference_for.count(fasta_name));
                FastaReference* reference = reference_for[fasta_name];

                // We'll set this to true if we actually find the VCF that contains
                // the variants for this sequence and successfully build the graph for it.
                bool built_region = false;
                
                for (auto& buffer : buffers) {
                    // For each VCF we are going to read
                    if(!buffer->has_tabix()) {
                        // Die if we don't have indexes for everyone.
                        // TODO: report errors to caller instead.
#pragma omp critical (cerr)
                        cerr << "[vg::Constructor] Error: all VCFs must be indexed when restricting to a region" << endl;
                        exit(1);
                    }

                    // We set this to true if this VCF contains this region.
                    bool in_this_vcf = false;

                    // Try seeking to the right contig/region
                    if (allowed_vcf_regions.count(vcf_name)) {
                        // Seek to just that region (0-based)
                        in_this_vcf = buffer->set_region(vcf_name, allowed_vcf_regions[vcf_name].first,
                                allowed_vcf_regions[vcf_name].second);
                    } else {
                        // Seek to just the whole contig
                        in_this_vcf = buffer->set_region(vcf_name);
                    }

                    if (in_this_vcf) {
                        // This VCF covers the region
                        
                        if (built_region) {
                            // The region has already been built; we are checking for conflicting VCFs and we found one.
                            // TODO: Use them all with some kind of on-the-fly merging version of the variant buffer.
#pragma omp critical (cerr)
                            cerr << "[vg::Constructor] Error: multiple VCFs cover selected region in " << vcf_name
                                << "; merge them before constructing the graph" << endl;
                            exit(1);
                        } else {
                            // This buffer is the one!
                            // Construct the graph for this contig with the FASTA and the VCF.
                            construct_graph(vcf_name, *reference, *buffer, insertions, callback);
                            
                            // Record that we built the region but check the
                            // other VCFs still so we can complain if the user
                            // gave us overlapping VCFs we can't use.
                            built_region = true;
                        }
                    }
                }

                if (!built_region) {
                    // None of the VCFs include variants on this sequence.
                    // Just build the graph for this sequence with no varaints.
                    VcfBuffer empty(nullptr);
                    construct_graph(vcf_name, *reference, empty, insertions, callback);
                }
            }
        } else {
            // If we have no set of contigs

            // Keep track of the contigs we have constructed, by VCF name
            set<string> constructed;

            for (auto& buffer : buffers) {
                // Go through all the VCFs
                // TODO: do this in parallel

                // Peek at the first variant and see its contig
                buffer->fill_buffer();
                while(buffer->get()) {
                    // While there are still variants in the file
                    // See what contig the next varianmt is on.
                    string vcf_contig = buffer->get()->sequenceName;
                    
                    if (constructed.count(vcf_contig)) {
                        // We already did this contig. The user must have
                        // passed us overlapping, unmerged VCFs which we can't
                        // support yet.
                        cerr << "[vg::Constructor] Error: multiple VCFs cover " << vcf_contig
                            << "; merge them before constructing the graph" << endl;
                        exit(1);
                    }

                    // Decide what FASTA contig that is and make sure we have it
                    string fasta_contig = vcf_to_fasta(vcf_contig);
                    assert(reference_for.count(fasta_contig));
                    auto* reference = reference_for[fasta_contig];

                    // Construct on it with the appropriate FastaReference for that contig
                    construct_graph(vcf_contig, *reference, *buffer, insertions, callback);
                    // Remember we did this one
                    constructed.insert(vcf_contig);

                    // After we're done constructing, scan until VCF EOF or a new contig comes up
                    buffer->fill_buffer();
                    while (buffer->get() && buffer->get()->sequenceName == vcf_contig) {
                        // Discard anything left on the same contig, since it must be
                        // out of our desired interval for that contig.
                        buffer->handle_buffer();
                        buffer->fill_buffer();
                    }
                }
            }

            // Then for all the FASTA contigs that didn't appear in the VCFs,
            // construct them with no variants.

            for (auto& kv : reference_for) {
                // For every FASTA contig (and the reference that holds it)
                auto& fasta_contig = kv.first;
                FastaReference* reference = kv.second;

                // Convert the name to VCF space
                auto vcf_contig = fasta_to_vcf(fasta_contig);

                if (constructed.count(vcf_contig)) {
                    // Skip contigs we already did in the VCF
                    continue;
                }

                // Construct all the contigs we didn't do yet with no varaints.
                VcfBuffer empty(nullptr);
                construct_graph(vcf_contig, *reference, empty, insertions, callback);
            }

            // Now we've constructed everything we can. We're done!


        }

    }

}


