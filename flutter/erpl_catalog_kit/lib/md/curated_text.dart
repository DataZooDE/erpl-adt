import 'package:flutter/material.dart';

/// Renders curated text (biz_definition) — a deliberately minimal
/// markdown-like renderer (bold **text**, bullet "- " lines) rather than a
/// full CommonMark implementation, since curated definitions are short
/// glossary entries, not long-form documents.
class CuratedText extends StatelessWidget {
  final String text;
  const CuratedText({super.key, required this.text});

  @override
  Widget build(BuildContext context) {
    final lines = text.split('\n');
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        for (final line in lines) _buildLine(context, line),
      ],
    );
  }

  Widget _buildLine(BuildContext context, String line) {
    final trimmed = line.trimLeft();
    if (trimmed.startsWith('- ') || trimmed.startsWith('* ')) {
      return Padding(
        padding: const EdgeInsets.only(left: 12, bottom: 2),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('•  '),
            Expanded(child: _buildRichLine(context, trimmed.substring(2))),
          ],
        ),
      );
    }
    if (trimmed.isEmpty) return const SizedBox(height: 6);
    return Padding(
      padding: const EdgeInsets.only(bottom: 2),
      child: _buildRichLine(context, line),
    );
  }

  Widget _buildRichLine(BuildContext context, String line) {
    final spans = <TextSpan>[];
    final boldPattern = RegExp(r'\*\*(.+?)\*\*');
    var last = 0;
    for (final match in boldPattern.allMatches(line)) {
      if (match.start > last) spans.add(TextSpan(text: line.substring(last, match.start)));
      spans.add(TextSpan(text: match.group(1), style: const TextStyle(fontWeight: FontWeight.bold)));
      last = match.end;
    }
    if (last < line.length) spans.add(TextSpan(text: line.substring(last)));
    return RichText(text: TextSpan(style: DefaultTextStyle.of(context).style, children: spans));
  }
}
