<?php
/**
 * @file
 * Theme code for hosting.module.
 */

use SensioLabs\AnsiConverter\Theme\SolarizedXTermTheme;
use SensioLabs\AnsiConverter\AnsiToHtmlConverter;

/**
 * Output pretty console themed ASCII.
 *
 * Usage:
 *   theme('hosting_ascii', $string);
 */
function theme_hosting_ascii($vars) {
  $string = $vars['output'];
  if (empty($string)) {
    return $string;
  }

  // Prepare ASCII Converter
  $theme = new SolarizedXTermTheme();
  $styles = $theme->asCss();
  $styles .= ".ansi_box { overflow: auto; padding: 10px 15px; font-family: monospace; }";

  drupal_add_html_head("<style>$styles</style>");
  $converter = new AnsiToHtmlConverter($theme);
  $output = $converter->convert($string);

  // Make URLs clickable:
  $filter = new stdClass();
  $filter->settings = [
    'filter_url_length' => 1000,
  ];
  $output = _filter_url($output, $filter);

  // Change linebreaks to <br> tags.
  $output = strtr($output, array(
    '\n' => '<br />',
  ));

  return <<<HTML
  <pre class="ansi_color_bg_black ansi_color_fg_white ansi_box">$output</pre>
HTML;

}