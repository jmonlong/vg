
#include <thread>

#include "transcriptome.hpp"

namespace vg {

using namespace std;

// Number of transcripts buffered for each thread
static const int32_t num_thread_transcripts = 100;

void Transcriptome::add_transcripts(istream & transcript_stream, VG & graph, const gbwt::GBWT & haplotype_index) {

    vector<Transcript> transcripts;
    transcripts.reserve(num_thread_transcripts * num_threads);

    // Get mean length of nodes in the graph.
    const float mean_node_length = graph.length() / static_cast<double>(graph.size());
    pair<string, PathIndex *> chrom_path_index("", nullptr);

    int32_t line_number = 0;

    string chrom;
    string feature;

    string pos;

    string strand;
    string attributes;

    smatch regex_id_match;

    // Regex used to extract transcript name/id.
    regex regex_id_exp(transcript_tag + "\\s{1}\"?([^\"]*)\"?");

    while (transcript_stream.good()) {

        line_number += 1;
        getline(transcript_stream, chrom, '\t');

        // Skip header.
        if (chrom.empty() || chrom.front() == '#') {

            transcript_stream.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }

        if (!graph.paths.has_path(chrom)) {
        
            cerr << "[vg rna] ERROR: Chromomsome path \"" << chrom << "\" not found in graph (line " << line_number << ")." << endl;
            exit(1);

        } else if (chrom_path_index.first != chrom) {

            delete chrom_path_index.second;
            chrom_path_index.first = chrom;

            // Construct path index for chromosome/contig.
            chrom_path_index.second = new PathIndex(graph, chrom);
        }

        assert(chrom_path_index.second);

        transcript_stream.ignore(numeric_limits<streamsize>::max(), '\t');         
        getline(transcript_stream, feature, '\t');

        // Skip all non exon features, such as cds, gene etc.
        if (feature != "exon") {

            transcript_stream.ignore(numeric_limits<streamsize>::max(), '\n');  
            continue;
        }

        // Parse start and end exon position and convert to 0-base. 
        getline(transcript_stream, pos, '\t');
        int32_t spos = stoi(pos) - 1;
        getline(transcript_stream, pos, '\t');
        int32_t epos = stoi(pos) - 1;

        assert(spos <= epos);

        // Skip score column.
        transcript_stream.ignore(numeric_limits<streamsize>::max(), '\t');  
        
        // Parse strand and set whether it is reverse.
        getline(transcript_stream, strand, '\t');
        assert(strand == "+" || strand == "-");
        bool is_reverse = (strand == "-") ? true : false;

        // Skip frame column.
        transcript_stream.ignore(numeric_limits<streamsize>::max(), '\t');  

        getline(transcript_stream, attributes, '\n');

        string transcript_id = "";

        // Get transcript name/id from attribute column using regex.
        if (std::regex_search(attributes, regex_id_match, regex_id_exp)) {

            assert(regex_id_match.size() == 2);
            transcript_id = regex_id_match[1];
        }

        if (transcript_id.empty()) {

            cerr << "[vg rna] ERROR: Tag \"" << transcript_tag << "\" not found in attributes \"" << attributes << "\" (line " << line_number << ")." << endl;
            exit(1);
        }

        // Is this a new transcript.
        if (transcripts.empty()) {

            transcripts.emplace_back(Transcript(transcript_id, is_reverse, chrom));
        
        // Is this a new transcript.
        } else if (transcripts.back().name != transcript_id) {

            // Reorder reversed order exons.
            reorder_exons(&transcripts.back());

            // Is transcript buffer full. 
            if (transcripts.size() == num_thread_transcripts * num_threads) {

                // Construct transcript paths from transcripts in buffer.
                project_transcripts(transcripts, graph, haplotype_index, mean_node_length);
                transcripts.clear();
            }

            transcripts.emplace_back(Transcript(transcript_id, is_reverse, chrom));
        }

        assert(transcripts.back().is_reverse == is_reverse);

        // Add exon to current transcript.
        add_exon(&(transcripts.back()), make_pair(spos, epos), *chrom_path_index.second);
    }

    // Construct transcript paths from transcripts in buffer.
    project_transcripts(transcripts, graph, haplotype_index, mean_node_length);

    delete chrom_path_index.second;
}

void Transcriptome::add_exon(Transcript * transcript, const pair<int32_t, int32_t> & exon_pos, const PathIndex & chrom_path_index) const {

    transcript->exons.emplace_back(exon_pos);

    // Find path positions (node start position and id) of exon boundaries using path index.
    auto chrom_path_index_start_it = chrom_path_index.find_position(exon_pos.first);
    auto chrom_path_index_end_it = chrom_path_index.find_position(exon_pos.second);

    assert(chrom_path_index_start_it != chrom_path_index.end());
    assert(chrom_path_index_end_it != chrom_path_index.end());

    assert(chrom_path_index_start_it->first <= exon_pos.first);
    assert(chrom_path_index_end_it->first <= exon_pos.second);

    transcript->exon_nodes.emplace_back(Position(), Position());

    // Set node id of exon boundaries.
    transcript->exon_nodes.back().first.set_node_id(chrom_path_index_start_it->second.node);
    transcript->exon_nodes.back().second.set_node_id(chrom_path_index_end_it->second.node);

    // Set node offset of exon boundaries. 
    transcript->exon_nodes.back().first.set_offset(exon_pos.first - chrom_path_index_start_it->first);
    transcript->exon_nodes.back().second.set_offset(exon_pos.second - chrom_path_index_end_it->first);

    // Set whether exon node boundaries are reverse.
    transcript->exon_nodes.back().first.set_is_reverse(transcript->is_reverse);
    transcript->exon_nodes.back().second.set_is_reverse(transcript->is_reverse);
}

void Transcriptome::reorder_exons(Transcript * transcript) const {

    if (transcript->is_reverse) {

        // Is exons in reverse order.
        bool is_reverse_order = true;
        for (size_t i = 1; i < transcript->exons.size(); i++) {

            if (transcript->exons[i].second >= transcript->exons[i-1].first) { 

                is_reverse_order = false; 
            }
        }

        // Reverse if exons are in reverse order.
        if (is_reverse_order) { 

            reverse(transcript->exons.begin(), transcript->exons.end()); 
            reverse(transcript->exon_nodes.begin(), transcript->exon_nodes.end()); 
        }
    }
}

void Transcriptome::project_transcripts(const vector<Transcript> & transcripts, VG & graph, const gbwt::GBWT & haplotype_index, const float mean_node_length) {

    vector<thread> projection_threads;
    projection_threads.reserve(num_threads);

    // Spawn projection threads.
    for (int32_t thread_idx = 0; thread_idx < num_threads; thread_idx++) {

        projection_threads.push_back(thread(&Transcriptome::project_transcripts_callback, this, thread_idx, ref(transcripts), ref(graph), ref(haplotype_index), mean_node_length));
    }

    // Join projection threads.   
    for (auto & thread: projection_threads) {
        
        thread.join();
    }
}

void Transcriptome::project_transcripts_callback(const int32_t thread_idx, const vector<Transcript> & transcripts, VG & graph, const gbwt::GBWT & haplotype_index, const float mean_node_length) {

    list<TranscriptPath> thread_transcript_paths;

    int32_t transcripts_idx = thread_idx;

    while (transcripts_idx < transcripts.size()) {

        // Get next transcript belonging to current thread.
        const Transcript & transcript = transcripts.at(transcripts_idx);

        list<TranscriptPath> cur_transcript_paths;

        if (!haplotype_index.empty()) { 

            // Project transcript onto haplotypes in GBWT index.
            cur_transcript_paths = project_transcript_gbwt(transcript, graph, haplotype_index, mean_node_length); 
        }

        if (use_embedded_paths) { 

            // Project transcript onto embedded paths.
            cur_transcript_paths.splice(cur_transcript_paths.end(), project_transcript_embedded(transcript, graph)); 
        }

        if (collapse_transcript_paths) { 

            // Collapse identical transcript paths.
            collapse_identical_paths(&cur_transcript_paths);
        } 

        auto cur_transcript_paths_it = cur_transcript_paths.begin();
        int32_t transcript_path_idx = 1;

        while (cur_transcript_paths_it != cur_transcript_paths.end()) {

            assert(cur_transcript_paths_it->num_total >= cur_transcript_paths_it->num_reference);

            // Filter transcripts paths originating from a reference chromosome/contig.
            if (filter_reference_transcript_paths && cur_transcript_paths_it->num_reference > 0) {

                cur_transcript_paths_it->num_total -= cur_transcript_paths_it->num_reference;
                cur_transcript_paths_it->num_reference = 0;
                
                // Delete transcript path if all copies were of reference origin. 
                if (cur_transcript_paths_it->num_total == 0) {

                    cur_transcript_paths_it = cur_transcript_paths.erase(cur_transcript_paths_it);
                    continue;
                }
            }

            // Set transcript path name. The name contains the original transcript name/id, 
            // an unique index for each copy of the transcript, the number of identical 
            // copies (will be 1 if identical transcripts are not collapsed) and the number
            // of reference copies (will be 0 or 1 if identical transcripts are not collapsed).
            cur_transcript_paths_it->path.set_name(transcript.name + "_" + to_string(transcript_path_idx) + "_" + to_string(cur_transcript_paths_it->num_total) + "_" + to_string(cur_transcript_paths_it->num_reference));
            ++transcript_path_idx;

            ++cur_transcript_paths_it;
        }

        thread_transcript_paths.splice(thread_transcript_paths.end(), cur_transcript_paths);
        transcripts_idx += num_threads;
    }

    lock_guard<mutex> trancriptome_lock(trancriptome_mutex);

    // Add transcript paths to transcriptome.
    _transcriptome.reserve(_transcriptome.size() + thread_transcript_paths.size());
    for (auto & transcript_path: thread_transcript_paths) {

        _transcriptome.emplace_back(move(transcript_path.path));
    }
}

list<TranscriptPath> Transcriptome::project_transcript_gbwt(const Transcript & cur_transcript, VG & graph, const gbwt::GBWT & haplotype_index, const float mean_node_length) const {

    list<TranscriptPath> cur_transcript_paths;

    vector<pair<vector<exon_nodes_t>, int32_t> > haplotypes;
    unordered_map<int32_t, pair<int32_t, int32_t> > haplotype_id_index;

    for (int32_t exon_idx = 0; exon_idx < cur_transcript.exons.size(); ++exon_idx) {

        // Calculate expected number of nodes between exon start and end.
        const int32_t expected_length = ceil((cur_transcript.exons.at(exon_idx).second - cur_transcript.exons.at(exon_idx).first) / mean_node_length);

        // Get all haplotypes in GBWT index between exon start and end nodes.
        auto exon_haplotypes = get_exon_haplotypes(cur_transcript.exon_nodes.at(exon_idx).first.node_id(), cur_transcript.exon_nodes.at(exon_idx).second.node_id(), haplotype_index, expected_length);

        if (haplotypes.empty()) {

            for (auto & exon_haplotype: exon_haplotypes) {

                haplotypes.emplace_back(vector<exon_nodes_t>(1, exon_haplotype.first), exon_haplotype.second.size());
                haplotypes.back().first.reserve(cur_transcript.exons.size());

                for (auto & haplotype_id: exon_haplotype.second) {

                    assert(haplotype_id_index.emplace(haplotype_id, make_pair(haplotypes.size() - 1, exon_idx + 1)).second);
                }
            }
            
        } else {

            for (auto & exon_haplotype: exon_haplotypes) {

                assert(!exon_haplotype.first.empty());
                unordered_map<int32_t, uint32_t> extended_haplotypes;

                for (auto & haplotype_id: exon_haplotype.second) {

                    auto haplotype_id_index_it = haplotype_id_index.find(haplotype_id);

                    if (haplotype_id_index_it == haplotype_id_index.end()) {

                        continue;         
                    }

                    if (exon_idx != haplotype_id_index_it->second.second) {

                        assert(haplotype_id_index_it->second.second < exon_idx);
                        haplotype_id_index.erase(haplotype_id_index_it);
                        continue;
                    }

                    haplotype_id_index_it->second.second++;
                    pair<vector<exon_nodes_t>, int32_t> & cur_haplotype = haplotypes.at(haplotype_id_index_it->second.first);

                    if (extended_haplotypes.find(haplotype_id_index_it->second.first) != extended_haplotypes.end()) {

                        assert(cur_haplotype.first.size() == exon_idx + 1);
                        haplotypes.at(extended_haplotypes.at(haplotype_id_index_it->second.first)).second += 1;
                        haplotype_id_index_it->second.first = extended_haplotypes.at(haplotype_id_index_it->second.first);                       
                        continue;
                    }

                    if (cur_haplotype.first.size() == exon_idx) {

                        cur_haplotype.first.emplace_back(exon_haplotype.first);
                        cur_haplotype.second = 1;
                        assert(extended_haplotypes.emplace(haplotype_id_index_it->second.first, haplotype_id_index_it->second.first).second);
                    
                    } else if (cur_haplotype.first.size() == exon_idx + 1) {

                        haplotypes.emplace_back(vector<exon_nodes_t>(cur_haplotype.first.begin(), cur_haplotype.first.end() - 1), 1);
                        haplotypes.back().first.emplace_back(exon_haplotype.first);

                        assert(extended_haplotypes.emplace(haplotype_id_index_it->second.first, haplotypes.size() - 1).second);
                        haplotype_id_index_it->second.first = haplotypes.size() - 1;                
                    
                    } else {

                        haplotype_id_index.erase(haplotype_id_index_it);
                    } 
                }
            }
        }   
    }

    for (auto & haplotype: haplotypes) {

        // Skip partial transcript paths.
        // TODO: Add support for partial transcript paths.
        if (haplotype.first.size() != cur_transcript.exons.size()) {

            continue;
        }

        cur_transcript_paths.emplace_back(false);
        cur_transcript_paths.back().num_total = haplotype.second;

        for (int32_t exon_idx = 0; exon_idx < cur_transcript.exons.size(); ++exon_idx) {

            auto exons_start = cur_transcript.exon_nodes.at(exon_idx).first;
            auto exons_end = cur_transcript.exon_nodes.at(exon_idx).second;

            assert(gbwt::Node::id(haplotype.first.at(exon_idx).front()) == exons_start.node_id());
            assert(gbwt::Node::id(haplotype.first.at(exon_idx).back()) == exons_end.node_id());

            for (auto & exon_node: haplotype.first.at(exon_idx)) {

                auto node_id = gbwt::Node::id(exon_node);
                auto node_length = graph.get_node(node_id)->sequence().size();

                int32_t offset = (node_id == exons_start.node_id()) ? exons_start.offset() : 0;
                assert(0 <= offset && offset < node_length);

                int32_t edit_length = (node_id == exons_end.node_id()) ? (exons_end.offset() - offset + 1) : (node_length - offset);
                assert(0 < edit_length && edit_length <= node_length);

                // Add new mapping in forward direction. Later the whole path will
                // be reverse complemented if transcript is on the '-' strand.
                auto new_mapping = cur_transcript_paths.back().path.add_mapping();
                new_mapping->mutable_position()->set_node_id(node_id);
                new_mapping->mutable_position()->set_offset(offset);
                new_mapping->mutable_position()->set_is_reverse(false);

                // Add new edit representing a complete match.
                auto new_edit = new_mapping->add_edit();
                new_edit->set_from_length(edit_length);
                new_edit->set_to_length(edit_length);
            }
        }

        assert(cur_transcript_paths.back().path.mapping_size() > 0);

        if (cur_transcript.is_reverse) {

            // Reverse complement transcript paths that are on the '-' strand.
            reverse_complement_path_in_place(&(cur_transcript_paths.back().path), [&](size_t node_id) {return graph.get_node(node_id)->sequence().size();});
        }

        // Copy paths if collapse of identical transcript paths is not wanted.
        if (!collapse_transcript_paths) {

            int32_t cur_transcript_path_num_total = cur_transcript_paths.back().num_total;
            cur_transcript_paths.back().num_total = 1;

            // Create 'num_total' of identical copies.
            for (int32_t i = 1; i < cur_transcript_path_num_total; ++i) {

                cur_transcript_paths.emplace_back(cur_transcript_paths.back());
            }
        }
    }

    return cur_transcript_paths; 
}

vector<pair<exon_nodes_t, vector<gbwt::size_type> > > Transcriptome::get_exon_haplotypes(const vg::id_t start_node, const vg::id_t end_node, const gbwt::GBWT & haplotype_index, const int32_t expected_length) const {

    assert(expected_length > 0);

    // Calculate the expected upperbound of the length between the two 
    // nodes (number of nodes). 
    const int32_t expected_length_upperbound = 1.1 * expected_length;

    // Calcuate frequency for how often a check on whether an extension 
    // should be terminated is performed. 
    const int32_t termination_frequency = ceil(0.1 * expected_length);

    // Get ids for haplotypes that contain the end node.
    unordered_set<int32_t> end_haplotype_ids;
    for (auto & haplotype_id: haplotype_index.locate(haplotype_index.find(gbwt::Node::encode(end_node, false)))) {

        end_haplotype_ids.emplace(haplotype_id);
    }

    vector<pair<exon_nodes_t, vector<gbwt::size_type> > > exon_haplotypes;

    // Initialise haplotype extension queue on the start node.
    std::queue<pair<exon_nodes_t, gbwt::SearchState> > exon_haplotype_queue;
    exon_haplotype_queue.push(make_pair(exon_nodes_t(1, gbwt::Node::encode(start_node, false)), haplotype_index.find(gbwt::Node::encode(start_node, false))));
    exon_haplotype_queue.front().first.reserve(expected_length_upperbound);

    // Empty queue if no haplotypes containing the start node exist. 
    if (exon_haplotype_queue.front().second.empty()) { exon_haplotype_queue.pop(); }

    // Perform depth-first haplotype extension.
    while (!exon_haplotype_queue.empty()) {

        pair<exon_nodes_t, gbwt::SearchState> & cur_exon_haplotype = exon_haplotype_queue.front();

        // Stop current extension if end node is reached.
        if (gbwt::Node::id(cur_exon_haplotype.first.back()) == end_node) {

            exon_haplotypes.emplace_back(cur_exon_haplotype.first, haplotype_index.locate(cur_exon_haplotype.second));
            exon_haplotype_queue.pop();
            continue;            
        }

        // Check whether any haplotypes in the current extension contains the
        // end node. If not, end current extension. This check is only performed
        // after the upperbound on the expected number of nodes is reached. 
        if (cur_exon_haplotype.first.size() >= expected_length_upperbound && (cur_exon_haplotype.first.size() % termination_frequency) == 0) {

            bool has_relevant_haplotype = false;

            for (auto & haplotype_id: haplotype_index.locate(cur_exon_haplotype.second)) {

                if (end_haplotype_ids.find(haplotype_id) != end_haplotype_ids.end()) {

                    has_relevant_haplotype = true;
                    break;
                }
            }

            if (!has_relevant_haplotype) {

                exon_haplotype_queue.pop();
                continue;                
            }
        }
        
        auto out_edges = haplotype_index.edges(cur_exon_haplotype.first.back());

        // End current extension if no outgoing edges exist.
        if (out_edges.empty()) {

            exon_haplotype_queue.pop();
            continue;
        }

        auto out_edges_it = out_edges.begin(); 
        ++out_edges_it;

        while (out_edges_it != out_edges.end()) {

            auto extended_search = haplotype_index.extend(cur_exon_haplotype.second, out_edges_it->first);

            // Add new extension to queue if not empty (haplotypes found).
            if (!extended_search.empty()) { 

                exon_haplotype_queue.push(make_pair(cur_exon_haplotype.first, extended_search));
                exon_haplotype_queue.back().first.emplace_back(out_edges_it->first);
            }

            ++out_edges_it;
        }

        cur_exon_haplotype.first.emplace_back(out_edges.begin()->first);
        cur_exon_haplotype.second = haplotype_index.extend(cur_exon_haplotype.second, out_edges.begin()->first);        

        // End current extension if empty (no haplotypes found). 
        if (cur_exon_haplotype.second.empty()) { exon_haplotype_queue.pop(); }
    }

    return exon_haplotypes;
}

list<TranscriptPath> Transcriptome::project_transcript_embedded(const Transcript & cur_transcript, VG & graph) const {

    vector<map<int64_t, set<mapping_t*> > *> exon_start_node_mappings;
    vector<map<int64_t, set<mapping_t*> > *> exon_end_node_mappings;

    exon_start_node_mappings.reserve(cur_transcript.exon_nodes.size());
    exon_end_node_mappings.reserve(cur_transcript.exon_nodes.size());

    // Get embedded path ids and node mappings for all exon node boundaries in transcript.
    for (auto & exon_node: cur_transcript.exon_nodes) {

        exon_start_node_mappings.emplace_back(&graph.paths.get_node_mapping(exon_node.first.node_id()));
        exon_end_node_mappings.emplace_back(&graph.paths.get_node_mapping(exon_node.second.node_id()));
    }

    list<TranscriptPath> cur_transcript_paths;

    // Loop over all paths that contain the transcript start node.
    for (auto & path_mapping_start: *exon_start_node_mappings.front()) {

        // Skip path if transcript end node is not in the current path.
        if (exon_end_node_mappings.back()->find(path_mapping_start.first) == exon_end_node_mappings.back()->end()) {

            continue;
        }

        // Skip alternative allele paths (_alt).
        if (Paths::is_alt(graph.paths.get_path_name(path_mapping_start.first))) {

            continue;
        }

        const bool is_reference_path = (graph.paths.get_path_name(path_mapping_start.first) == cur_transcript.chrom);
        bool is_partial = false;

        // Construct transcript path and set whether it originated from a reference chromosome/contig.
        cur_transcript_paths.emplace_back(is_reference_path);

        mapping_t * haplotype_path_start_map = nullptr;
        mapping_t * haplotype_path_end_map = nullptr;

        for (int32_t exon_idx = 0; exon_idx < exon_start_node_mappings.size(); ++exon_idx) {

            auto haplotype_path_start_it = exon_start_node_mappings.at(exon_idx)->find(path_mapping_start.first);
            auto haplotype_path_end_it = exon_end_node_mappings.at(exon_idx)->find(path_mapping_start.first);

            // Get path mapping at exon start if exon start node is in the current path.
            if (haplotype_path_start_it != exon_start_node_mappings.at(exon_idx)->end()) {

                assert(haplotype_path_start_it->second.size() == 1);
                haplotype_path_start_map = *haplotype_path_start_it->second.begin();
            }

            // Get path mapping at exon end if exon end node is in the current path.
            if (haplotype_path_end_it != exon_end_node_mappings.at(exon_idx)->end()) {

                assert(haplotype_path_end_it->second.size() == 1);
                haplotype_path_end_map = *haplotype_path_end_it->second.begin();
            }

            // Transcript paths are partial if either the start or end exon path 
            // mapping is empty. Partial transcripts are currently not supported.
            // TODO: Add support for partial transcript paths.
            if (!haplotype_path_start_map or !haplotype_path_end_map) {

                is_partial = true;
                break;
            }

            bool is_first_mapping = true;

            while (true) {

                auto cur_node_id = haplotype_path_start_map->node_id();
                auto node_length = graph.get_node(cur_node_id)->sequence().size();
                assert(node_length == haplotype_path_start_map->length);

                int32_t offset = (is_first_mapping) ? cur_transcript.exon_nodes.at(exon_idx).first.offset() : 0;
                assert(0 <= offset && offset < node_length);

                int32_t edit_length = (haplotype_path_start_map == haplotype_path_end_map) ? (cur_transcript.exon_nodes.at(exon_idx).second.offset() - offset + 1) : (node_length - offset);
                assert(0 < edit_length && edit_length <= node_length);

                // Add new mapping in forward direction. Later the whole path will
                // be reverse complemented if transcript is on the '-' strand.
                auto new_mapping = cur_transcript_paths.back().path.add_mapping();
                new_mapping->mutable_position()->set_node_id(cur_node_id);
                new_mapping->mutable_position()->set_offset(offset);
                new_mapping->mutable_position()->set_is_reverse(false);

                // Add new edit representing a complete match.
                auto new_edit = new_mapping->add_edit();
                new_edit->set_from_length(edit_length);
                new_edit->set_to_length(edit_length);
                
                if (haplotype_path_start_map == haplotype_path_end_map) { break; }

                haplotype_path_start_map = graph.paths.traverse_right(haplotype_path_start_map);
                assert(haplotype_path_start_map);
                
                is_first_mapping = false;
            }

            haplotype_path_start_map = nullptr;
            haplotype_path_end_map = nullptr;
        }

        if (is_partial) {

            // Delete partial transcript paths.
            cur_transcript_paths.pop_back();
        
        } else {

            assert(cur_transcript_paths.back().path.mapping_size() > 0);

            // Reverse complement transcript paths that are on the '-' strand.
            if (cur_transcript.is_reverse) {

                reverse_complement_path_in_place(&(cur_transcript_paths.back().path), [&](size_t node_id) {return graph.get_node(node_id)->sequence().size();});
            } 
        }       
    } 

    return cur_transcript_paths;
}

void Transcriptome::collapse_identical_paths(list<TranscriptPath> * cur_transcript_paths) const {

    auto cur_transcript_paths_it_1 = cur_transcript_paths->begin();

    while (cur_transcript_paths_it_1 != cur_transcript_paths->end()) {

        auto cur_transcript_paths_it_2 = cur_transcript_paths_it_1;
        ++cur_transcript_paths_it_2;

        while (cur_transcript_paths_it_2 != cur_transcript_paths->end()) {

            // Check if two path mappings are identical.
            if (equal(cur_transcript_paths_it_1->path.mapping().begin(), cur_transcript_paths_it_1->path.mapping().end(), cur_transcript_paths_it_2->path.mapping().begin(), [](const Mapping & m1, const Mapping & m2) { return google::protobuf::util::MessageDifferencer::Equals(m1, m2); })) {

                // Update identical transcript path copy number.
                cur_transcript_paths_it_1->num_total += cur_transcript_paths_it_2->num_total;
                cur_transcript_paths_it_1->num_reference += cur_transcript_paths_it_2->num_reference;

                // Delete one of the identical paths.
                cur_transcript_paths_it_2 = cur_transcript_paths->erase(cur_transcript_paths_it_2);

            } else {

                ++cur_transcript_paths_it_2;
            }
        }

        ++cur_transcript_paths_it_1;
    }
}

const vector<Path> & Transcriptome::transcript_paths() const {

    return _transcriptome;
}

int32_t Transcriptome::size() const {

    return _transcriptome.size();
}

void Transcriptome::edit_graph(VG * graph, const bool add_paths) {

    // Edit graph with transcript paths and update path traversals
    // to match the augmented graph. 
    graph->edit(_transcriptome, add_paths, true, true);
}

void Transcriptome::construct_gbwt(gbwt::GBWTBuilder * gbwt_builder) const {

    for (auto & path: _transcriptome) {

        // Convert path to GBWT thread.
        gbwt::vector_type gbwt_thread(path.mapping_size());
        for (size_t i = 0; i < path.mapping_size(); i++) {

            assert(path.mapping(i).edit_size() == 1);
            gbwt_thread[i] = gbwt::Node::encode(path.mapping(i).position().node_id(), path.mapping(i).position().is_reverse());
        }

        // Insert thread into GBWT index.
        gbwt_builder->insert(gbwt_thread, false);
    }
}

void Transcriptome::write_gam_alignments(ostream * gam_ostream) const {

    stream::ProtobufEmitter<Alignment> emitter(*gam_ostream);

    for (auto & path: _transcriptome) {

        // Write path as alignment 
        Alignment alignment;
        alignment.set_name(path.name());
        *alignment.mutable_path() = path;
        emitter.write(std::move(alignment));
    }
}

void Transcriptome::write_fasta_sequences(ostream * fasta_ostream, VG & graph) const {

    for (auto & path: _transcriptome) {

        // Write path name and sequence.
        *fasta_ostream << ">" << path.name() << endl;
        *fasta_ostream << graph.path_sequence(path) << endl;
    }
}
    
}





