#pragma once


#include <queue>



namespace simple_vgjs {


    struct thread_index_t { uint32_t value; };

    using thread_id_t = int_type<int, struct P1, -1>;
    using thread_type_t = int_type<int, struct P2, -1>;
    using thread_count_t = int_type<int, struct P3, -1>;
    using tag_t = int_type<int, struct P4, -1>;
    using parent_t = int_type<int, struct P5, -1>;



}


