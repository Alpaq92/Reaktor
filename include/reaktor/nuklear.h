#ifndef REAKTOR_NUKLEAR_H
#define REAKTOR_NUKLEAR_H

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT

/* A browser delivers press and release in one task. */
#define NK_BUTTON_TRIGGER_ON_RELEASE

#include <stddef.h>
#include <nuklear.h>

#define reaktor_bool             nk_bool
#define reaktor_byte             nk_byte
#define reaktor_color            nk_color
#define reaktor_colorf           nk_colorf
#define reaktor_command_buffer   nk_command_buffer
#define reaktor_context          nk_context
#define reaktor_flags            nk_flags
#define reaktor_hash             nk_hash
#define reaktor_image            nk_image
#define reaktor_list_view        nk_list_view
#define reaktor_panel            nk_panel
#define reaktor_plugin_filter    nk_plugin_filter
#define reaktor_rect             nk_rect
#define reaktor_size             nk_size
#define reaktor_style_button     nk_style_button
#define reaktor_style_edit       nk_style_edit
#define reaktor_style_item       nk_style_item
#define reaktor_style_selectable nk_style_selectable
#define reaktor_uint             nk_uint
#define reaktor_user_font        nk_user_font
#define reaktor_vec2             nk_vec2

#define REAKTOR_CHART_COLUMN        NK_CHART_COLUMN
#define REAKTOR_CHART_LINES         NK_CHART_LINES
#define REAKTOR_EDIT_BOX            NK_EDIT_BOX
#define REAKTOR_FIXED               NK_FIXED
#define REAKTOR_LEN                 NK_LEN
#define REAKTOR_MAXIMIZED           NK_MAXIMIZED
#define REAKTOR_MINIMIZED           NK_MINIMIZED
#define REAKTOR_MODIFIABLE          NK_MODIFIABLE
#define REAKTOR_POPUP_DYNAMIC       NK_POPUP_DYNAMIC
#define REAKTOR_STATIC              NK_STATIC
#define REAKTOR_STYLE_ITEM_COLOR    NK_STYLE_ITEM_COLOR
#define REAKTOR_TEXT_CENTERED       NK_TEXT_CENTERED
#define REAKTOR_TEXT_LEFT           NK_TEXT_LEFT
#define REAKTOR_TREE_NODE           NK_TREE_NODE
#define REAKTOR_TREE_TAB            NK_TREE_TAB
#define REAKTOR_WIDGET_RIGHT        NK_WIDGET_RIGHT
#define REAKTOR_WIDGET_STATE_HOVER  NK_WIDGET_STATE_HOVER
#define REAKTOR_WINDOW_BORDER       NK_WINDOW_BORDER
#define REAKTOR_WINDOW_NO_SCROLLBAR NK_WINDOW_NO_SCROLLBAR
#define REAKTOR_WINDOW_TITLE        NK_WINDOW_TITLE
#define reaktor_false               nk_false
#define reaktor_true                nk_true

#define reaktor_button_color                     nk_button_color
#define reaktor_button_image_label               nk_button_image_label
#define reaktor_button_plain                     nk_button_label
#define reaktor_chart_add_slot                   nk_chart_add_slot
#define reaktor_chart_begin                      nk_chart_begin
#define reaktor_chart_end                        nk_chart_end
#define reaktor_chart_push                       nk_chart_push
#define reaktor_chart_push_slot                  nk_chart_push_slot
#define reaktor_checkbox_flags_label             nk_checkbox_flags_label
#define reaktor_checkbox_label                   nk_checkbox_label
#define reaktor_checkbox_label_align             nk_checkbox_label_align
#define reaktor_color_cf                         nk_color_cf
#define reaktor_combo                            nk_combo
#define reaktor_contextual_begin                 nk_contextual_begin
#define reaktor_contextual_end                   nk_contextual_end
#define reaktor_contextual_item_label            nk_contextual_item_label
#define reaktor_draw_image                       nk_draw_image
#define reaktor_draw_text                        nk_draw_text
#define reaktor_fill_polygon                     nk_fill_polygon
#define reaktor_fill_rect                        nk_fill_rect
#define reaktor_filter_decimal                   nk_filter_decimal
#define reaktor_filter_default                   nk_filter_default
#define reaktor_filter_hex                       nk_filter_hex
#define reaktor_group_begin                      nk_group_begin
#define reaktor_group_begin_titled               nk_group_begin_titled
#define reaktor_group_end                        nk_group_end
#define reaktor_group_get_scroll                 nk_group_get_scroll
#define reaktor_group_set_scroll                 nk_group_set_scroll
#define reaktor_input_begin                      nk_input_begin
#define reaktor_input_end                        nk_input_end
#define reaktor_input_is_mouse_hovering_rect     nk_input_is_mouse_hovering_rect
#define reaktor_text_label                       nk_label
#define reaktor_label_colored                    nk_label_colored
#define reaktor_label_colored_wrap               nk_label_colored_wrap
#define reaktor_layout_row_begin                 nk_layout_row_begin
#define reaktor_layout_row_dynamic               nk_layout_row_dynamic
#define reaktor_layout_row_end                   nk_layout_row_end
#define reaktor_layout_row_push                  nk_layout_row_push
#define reaktor_layout_row_template_begin        nk_layout_row_template_begin
#define reaktor_layout_row_template_end          nk_layout_row_template_end
#define reaktor_layout_row_template_push_dynamic nk_layout_row_template_push_dynamic
#define reaktor_layout_row_template_push_static  nk_layout_row_template_push_static
#define reaktor_layout_space_begin               nk_layout_space_begin
#define reaktor_layout_space_end                 nk_layout_space_end
#define reaktor_layout_space_push                nk_layout_space_push
#define reaktor_list_view_begin                  nk_list_view_begin
#define reaktor_list_view_end                    nk_list_view_end
#define reaktor_menu_begin_label                 nk_menu_begin_label
#define reaktor_menu_end                         nk_menu_end
#define reaktor_menu_item_label                  nk_menu_item_label
#define reaktor_menu_item_symbol_label           nk_menu_item_symbol_label
#define reaktor_menubar_begin                    nk_menubar_begin
#define reaktor_menubar_end                      nk_menubar_end
#define reaktor_option_label                     nk_option_label
#define reaktor_plot                             nk_plot
#define reaktor_property_float                   nk_property_float
#define reaktor_push_scissor                     nk_push_scissor
#define reaktor_rgb                              nk_rgb
#define reaktor_rgb_cf                           nk_rgb_cf
#define reaktor_rgba                             nk_rgba
#define reaktor_rule_horizontal                  nk_rule_horizontal
#define reaktor_selectable_label                 nk_selectable_label
#define reaktor_spacer                           nk_spacer
#define reaktor_spacing                          nk_spacing
#define reaktor_stroke_line                      nk_stroke_line
#define reaktor_style_item_color                 nk_style_item_color
#define reaktor_style_item_hide                  nk_style_item_hide
#define reaktor_style_pop_color                  nk_style_pop_color
#define reaktor_style_pop_float                  nk_style_pop_float
#define reaktor_style_pop_font                   nk_style_pop_font
#define reaktor_style_pop_style_item             nk_style_pop_style_item
#define reaktor_style_pop_vec2                   nk_style_pop_vec2
#define reaktor_style_push_color                 nk_style_push_color
#define reaktor_style_push_float                 nk_style_push_float
#define reaktor_style_push_font                  nk_style_push_font
#define reaktor_style_push_style_item            nk_style_push_style_item
#define reaktor_style_push_vec2                  nk_style_push_vec2
#define reaktor_tooltip                          nk_tooltip
#define reaktor_tree_element                     nk_tree_element
#define reaktor_tree_element_push                nk_tree_element_push
#define reaktor_tree_pop                         nk_tree_pop
#define reaktor_tree_push                        nk_tree_push
#define reaktor_widget_bounds                    nk_widget_bounds
#define reaktor_widget_disable_begin             nk_widget_disable_begin
#define reaktor_window_get_bounds                nk_window_get_bounds
#define reaktor_window_get_canvas                nk_window_get_canvas
#define reaktor_window_get_content_region_size   nk_window_get_content_region_size

#endif
